//-------------------------------------------------------------------------
/*
Copyright (C) 2022 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#include "duke3d.h"
#include "addons.h"
#include "sjson.h"

#include "colmatch.h"
#include "kplib.h"
#include "vfs.h"

static const char* currentaddonfilename;

static const char grpext[] = "*.grp";
static const char ssiext[] = "*.ssi";
static const char* addonextensions[] = { grpext, ssiext, "*.zip", "*.pk3", "*.pk4" };

static const char addondirpath[] = "./addons";
static const char addonjsonfn_standard[] = "addon.json";
static const char addonjsonfn_83format[] = "addon.jso";

// Keys used in the JSON addon descriptor
static const char jsonkey_title[] = "title";
static const char jsonkey_author[] = "author";
static const char jsonkey_version[] = "version";
static const char jsonkey_desc[] = "description";
static const char jsonkey_image[] = "image";
static const char jsonkey_modtype[] = "type";
static const char jsonkey_dependency[] = "dependency";
static const char jsonkey_con[] = "CON";
static const char jsonkey_def[] = "DEF";
static const char jsonkey_rts[] = "RTS";
static const char jsonkey_scripttype[] = "type";
static const char jsonkey_scriptpath[] = "path";

//addon type strings
static const char jsonvalue_addonmain[] = "main";
static const char jsonvalue_addonmodule[] = "module";

menuaddon_t * g_menuaddons = nullptr;
uint16_t g_nummenuaddons = 0;

static void a_free_menuaddons(void)
{
    if (g_menuaddons != nullptr)
    {
        Xfree(g_menuaddons);
        g_menuaddons = nullptr;
    }
    g_nummenuaddons = 0;
}


static void a_updateaddonentryname(int32_t index)
{
    Bsprintf(g_menuaddons[index].entryname, "%d: %s", g_menuaddons[index].loadOrderIndex+1, g_menuaddons[index].jsonDat.title);
}


// This function copies the given string into the text buffer and adds linebreaks at appropriate locations.
//   * lblen : maximum number of characters until linebreak forced
// Returns the number of lines in the text.
static int32_t a_strncpy_textwrap(char* dst, const char *src, int32_t const maxlen, int32_t const lblen)
{
    // indices and line length
    int srcidx = 0, dstidx = 0, lastwsidx = 0;
    int currlinelength = 0;

    int32_t linecount = 1;
    while (src[srcidx] && (dstidx < maxlen - 8))
    {
        // track last whitespace index of destination
        if (isspace(src[srcidx]))
            lastwsidx = dstidx;
        dst[dstidx++] = src[srcidx++];

        if (src[srcidx-1] == '\n')
        {
            linecount++;
            currlinelength = 0;
        }
        else if (++currlinelength >= lblen)
        {
            if (dstidx - lastwsidx < (lblen >> 2))
            {
                // split at whitespace
                dst[lastwsidx] = '\n';
                currlinelength = dstidx - lastwsidx;
            }
            else
            {
                // split word (don't care about syllables )
                dst[dstidx++] = '-';
                dst[dstidx++] = '\n';
                currlinelength = 0;
            }
            linecount++;
            lastwsidx = dstidx;
        }
    }

    if (dstidx >= maxlen - 8)
    {
        Bstrcpy(&dst[dstidx], " [...]");
    }
    else
    {
        dst[dstidx] = '\0';
    }

    return linecount;
}

static int32_t a_checkfilepresence(const char* filepath)
{
    buildvfs_kfd jsonfil = kopen4loadfrommod(filepath, 0);
    bool loadsuccess = (jsonfil != buildvfs_kfd_invalid);
    kclose(jsonfil);
    return loadsuccess;
}

// Copy addon string from JSON into buffer and check for errors in the json
// Optionally perform textwrapping if lblen > 0
// Returns 0 on success, 1 if member not present, -1 on error
static int32_t a_parseaddonjson_stringmember(sjson_node* node, const char* key, char* dstbuf,
                                             int32_t const bufsize, int32_t const lblen, int32_t* linecount)
{
    sjson_node * ele = sjson_find_member_nocase(node, key);
    if (ele == nullptr)
        return 1;

    if (ele->tag != SJSON_STRING)
    {
        LOG_F(ERROR, "Addon descriptor member '%s' of addon '%s' is not string typed!", key, currentaddonfilename);
        return -1;
    }

    if (lblen <= 0)
        Bstrncpy(dstbuf, ele->string_, bufsize);
    else
        *linecount = a_strncpy_textwrap(dstbuf, ele->string_, bufsize, lblen);

    if (dstbuf[bufsize-1])
    {
        LOG_F(WARNING, "Member '%s' of addon '%s' exceeds maximum size of %d chars!", key, currentaddonfilename, bufsize);
        dstbuf[bufsize-1] = '\0';
    }
    return 0;
}

// Load script array from JSON
// Returns 0 on success, 1 if member not present, -1 on error
static int32_t a_parseaddonjson_scriptarray(sjson_node* root, const char* key, const char* basepath,
                                            char* mainscriptpath, char*** modules, int32_t* modulecount)
{
    sjson_node * nodes = sjson_find_member_nocase(root, key);
    if (nodes == nullptr)
        return 1;

    if (nodes->tag != SJSON_ARRAY)
    {
        LOG_F(ERROR, "Content of member '%s' of addon '%s' is not an array!", key, currentaddonfilename);
        return -1;
    }

    int numchildren = sjson_child_count(nodes);
    char** scriptmodules = (char **) Xmalloc(numchildren * sizeof(char*));

    char scriptbuf[BMAX_PATH];
    sjson_node* snode;
    int32_t numValidChildren = 0;
    sjson_foreach(snode, nodes)
    {
        if (snode->tag != SJSON_OBJECT)
        {
            LOG_F(ERROR, "Invalid json type in array of member '%s' of addon '%s'!", key, currentaddonfilename);
            continue;
        }

        sjson_node * script_path = sjson_find_member_nocase(snode, jsonkey_scriptpath);
        if (script_path == nullptr || script_path->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Script path missing or has invalid format in addon '%s'!", currentaddonfilename);
            continue;
        }

        Bsnprintf(scriptbuf, BMAX_PATH, "%s/%s", basepath, script_path->string_);
        if (!a_checkfilepresence(scriptbuf))
        {
            LOG_F(ERROR, "Script file of addon '%s' at location '%s' does not exist!", currentaddonfilename, scriptbuf);
            continue;
        }

        sjson_node * script_type = sjson_find_member_nocase(snode, jsonkey_scripttype);
        if (script_type == nullptr || script_type->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Script type missing or has invalid format in addon '%s'!", currentaddonfilename);
            continue;
        }

        if (!Bstrncasecmp(script_type->string_, jsonvalue_addonmain, ARRAY_SIZE(jsonvalue_addonmain)))
        {
            Bstrncpy(mainscriptpath, script_path->string_, BMAX_PATH);
        }
        else if (!Bstrncasecmp(script_type->string_, jsonvalue_addonmodule, ARRAY_SIZE(jsonvalue_addonmodule)))
        {
            scriptmodules[numValidChildren] = (char *) Xmalloc(BMAX_PATH * sizeof(char));
            Bstrncpy(scriptmodules[numValidChildren], script_path->string_, BMAX_PATH);
            numValidChildren++;
        }
        else
        {
            LOG_F(ERROR, "Invalid script type '%s' specified in addon '%s'!", script_type->string_, currentaddonfilename);
            continue;
        }
    }

    if (numValidChildren == 0)
    {
        Xfree(scriptmodules);
        *modules = nullptr;
    }
    else
    {
        *modules = (char **) Xrealloc(scriptmodules, numValidChildren * sizeof(char*));
    }
    *modulecount = numValidChildren;
    return 0;
}


static int32_t a_loadpreviewfromfile(char const * fn, char* imagebuffer)
{
#ifdef WITHKPLIB
    int32_t i, j, xsiz = 0, ysiz = 0;
    palette_t *picptr = NULL;

    kpzdecode(kpzbufload(fn), (intptr_t *)&picptr, &xsiz, &ysiz);
    if (xsiz != PREVIEWTILEX || ysiz != PREVIEWTILEY)
    {
        LOG_F(ERROR, "Addon preview image '%s' does not have required format: %dx%d", fn, PREVIEWTILEX, PREVIEWTILEY);
        return -2;
    }

    if (!(paletteloaded & PALETTE_MAIN))
    {
        LOG_F(ERROR, "Addon Preview: no palette loaded");
        return -3;
    }

    paletteFlushClosestColor();
    for (j = 0; j < ysiz; ++j)
    {
        int const ofs = j * xsiz;
        for (i = 0; i < xsiz; ++i)
        {
            palette_t const *const col = &picptr[ofs + i];
            imagebuffer[(i * ysiz) + j] = paletteGetClosestColorUpToIndex(col->b, col->g, col->r, 254);
        }
    }

    Xfree(picptr);
    return 0;
#else
    UNREFERENCED_CONST_PARAMETER(fn);
    UNREFERENCED_PARAMETER(imagebuffer);
    return -1;
#endif
}


static int32_t a_parseaddonjson_previewimage(sjson_node* root, char* imagebuffer, const char* basepath)
{
    sjson_node * ele = sjson_find_member_nocase(root, jsonkey_image);
    if (ele == nullptr)
        return -1;

    if (ele->tag != SJSON_STRING)
    {
        LOG_F(ERROR, "Provided image path for addon '%s' is not a string!", currentaddonfilename);
        return -1;
    }

    char fnbuf[BMAX_PATH];
    Bsnprintf(fnbuf, BMAX_PATH, "%s/%s", basepath, ele->string_);
    if (!a_checkfilepresence(fnbuf))
    {
        LOG_F(ERROR, "Preview image of addon '%s' at location '%s' does not exist!", currentaddonfilename, fnbuf);
        return -1;
    }

    if (a_loadpreviewfromfile(fnbuf, imagebuffer) < 0)
    {
        imagebuffer[0] = '\0';
        return -1;
    }

    return 0;
}


// retrieve information from addon.json -- returns 0 on success, -1 on error
static int32_t a_parseaddonjson(sjson_context* ctx, addonjson_t* mjsonStore, const char* basepath, char* raw_json)
{
    if (!sjson_validate(ctx, raw_json))
    {
        LOG_F(ERROR, "Invalid JSON structure for addon '%s'!", currentaddonfilename);
        return -1;
    }

    sjson_node * root = sjson_decode(ctx, raw_json);
    sjson_node * ele = nullptr;

    a_parseaddonjson_previewimage(root, mjsonStore->imageBuffer, basepath);

    // addon type
    ele = sjson_find_member_nocase(root, jsonkey_modtype);
    if ((ele == nullptr) || ele->tag != SJSON_STRING)
    {
        LOG_F(ERROR, "Member 'type' missing or has invalid format!");
        return -1;
    }
    else
    {
        if (!Bstrncasecmp(ele->string_, jsonvalue_addonmain, ARRAY_SIZE(jsonvalue_addonmain)))
        {
            mjsonStore->addonType = ATYPE_MAIN;
        }
        else if (!Bstrncasecmp(ele->string_, jsonvalue_addonmodule, ARRAY_SIZE(jsonvalue_addonmodule)))
        {
            mjsonStore->addonType = ATYPE_MODULE;
        }
        else
        {
            LOG_F(ERROR, "Invalid addon type specified: %s", ele->string_);
            return -1;
        }
    }

    // load visual descriptors (and defaults)
    if (a_parseaddonjson_stringmember(root, jsonkey_title, mjsonStore->title, MAXLEN_ADDONTITLE, -1, nullptr))
    {
        const char defaultTitle[] = "Unnamed Addon";
        Bstrncpy(mjsonStore->title, defaultTitle, ARRAY_SIZE(defaultTitle));
    }

    if (a_parseaddonjson_stringmember(root, jsonkey_author, mjsonStore->author, MAXLEN_ADDONAUTHOR, -1, nullptr))
    {
        const char defaultAuthor[] = "N/A";
        Bstrncpy(mjsonStore->author, defaultAuthor, ARRAY_SIZE(defaultAuthor));
    }

    if (a_parseaddonjson_stringmember(root, jsonkey_version, mjsonStore->version, MAXLEN_ADDONVERSION, -1, nullptr))
    {
        const char defaultVersion[] = "N/A";
        Bstrncpy(mjsonStore->version, defaultVersion, ARRAY_SIZE(defaultVersion));
    }

    int const lblen = (FURY) ? 84 : 64;
    if (a_parseaddonjson_stringmember(root, jsonkey_desc, mjsonStore->description,
                                        MAXLEN_ADDONDESC, lblen, &mjsonStore->desclinecount))
    {
        const char defaultDescription[] = "!!!Missing Description!!!";
        Bstrncpy(mjsonStore->description, defaultDescription, ARRAY_SIZE(defaultDescription));
    }

    // optional dependency
    ele = sjson_find_member_nocase(root, jsonkey_dependency);
    if (ele != nullptr)
    {
        if (ele->tag == SJSON_STRING)
        {
            //TODO: Transform string to a predefined CRC
            mjsonStore->dependencyCRC = 0;
        }
        else if (ele->tag == SJSON_NUMBER)
        {
            mjsonStore->dependencyCRC = (uint32_t) ele->number_;
        }
        else
        {
            LOG_F(ERROR, "Dependency must be an integer or a predefined string!");
            mjsonStore->dependencyCRC = 0;
        }
    }
    else
    {
        mjsonStore->dependencyCRC = 0;
    }

    // CON script loading
    a_parseaddonjson_scriptarray(root, jsonkey_con, basepath, mjsonStore->scriptNamePath,
                &mjsonStore->scriptModules, &mjsonStore->numCONModules);

    // DEF script loading
    a_parseaddonjson_scriptarray(root, jsonkey_def, basepath, mjsonStore->defNamePath,
                &mjsonStore->defModules, &mjsonStore->numDEFModules);

    // RTS file
    ele = sjson_find_member_nocase(root, jsonkey_rts);
    if (ele != nullptr)
    {
        if (ele->tag == SJSON_STRING)
        {
            Bsnprintf(mjsonStore->rtsNamePath, BMAX_PATH, "%s/%s", basepath, ele->string_);
            if (!a_checkfilepresence(mjsonStore->rtsNamePath))
            {
                LOG_F(ERROR, "Script file of addon '%s' at location '%s' does not exist!", currentaddonfilename, mjsonStore->rtsNamePath);
            }
        }
        else
        {
            LOG_F(ERROR, "Dependency must be an integer or a predefined string!");
        }
    }

    return 0;
}


static char* a_getaddondir()
{
    char * outfile_buf = (char*) Xmalloc(BMAX_PATH);

    if (g_modDir[0] != '/' || g_modDir[1] != 0)
        Bsnprintf(outfile_buf, BMAX_PATH, "%s/%s", g_modDir, addondirpath);
    else
        Bstrncpy(outfile_buf, addondirpath, BMAX_PATH);

    return outfile_buf;
}

// Count the number of addons present in the local folder, and the workshop folders.
static int32_t CountPotentialAddons(void)
{
    int32_t numaddons = 0;
    char * addondir = a_getaddondir();

    if (addondir)
    {
        fnlist_t fnlist = FNLIST_INITIALIZER;
        fnlist_clearnames(&fnlist);

        // get packages in the local addon dir
        for (auto & ext : addonextensions)
        {
            fnlist_getnames(&fnlist, addondir, ext, -1, 0);
            numaddons += fnlist.numfiles;
            fnlist_clearnames(&fnlist);
        }

        fnlist_getnames(&fnlist, addondir, "*", 0, -1);
        for (BUILDVFS_FIND_REC *rec = fnlist.finddirs; rec; rec=rec->next)
        {
            if (!strcmp(rec->name, ".")) continue;
            if (!strcmp(rec->name, "..")) continue;
            numaddons++;
        }
        fnlist_clearnames(&fnlist);

        Xfree(addondir);
    }

    // TODO: get number of workshop addon folders

    return numaddons;
}

static void a_packagecleanup(int32_t grpfileidx)
{
    if (grpfileidx < numgroupfiles)
        popgroupfile(); // remove grp/ssi
#ifdef WITHKPLIB
    else
        kzpopstack(); // remove zip
#endif
}

static int32_t LoadLocalPackagedAddons(sjson_context* ctx, fnlist_t* fnlist, const char* addondir)
{
    // look for addon packages
    for (auto & ext : addonextensions)
    {
        BUILDVFS_FIND_REC *rec;
        fnlist_getnames(fnlist, addondir, ext, -1, 0);

        for (rec=fnlist->findfiles; rec; rec=rec->next)
        {
            currentaddonfilename = rec->name;
            menuaddon_t & madd = g_menuaddons[g_nummenuaddons];
            madd.loadOrderIndex = g_nummenuaddons;

            // check file type based on extension
            if (!Bstrcmp(ext, grpext))
                madd.loadType = LT_GRP;
            else if (!Bstrcmp(ext, ssiext))
                madd.loadType = LT_SSI;
            else
                madd.loadType = LT_ZIP;

            char filepath[BMAX_PATH];
            Bsnprintf(filepath, ARRAY_SIZE(filepath), "%s/%s", addondir, rec->name);

            DLOG_F(INFO, "Attempting to load addon package file %s", filepath);

            const int32_t grpfileidx = initgroupfile(filepath);
            if (grpfileidx == -1)
            {
                LOG_F(ERROR, "Failed to open package at %s", filepath);
                madd.loadType = LT_INVALID;
                continue;
            }
            else if (grpfileidx >= numgroupfiles)
            {
                // file was a renamed zip, correct file type
                madd.loadType = LT_ZIP;
            }

            buildvfs_kfd jsonfil = buildvfs_kfd_invalid;
            if (madd.loadType == LT_GRP || madd.loadType == LT_SSI)
                jsonfil = kopen4load(addonjsonfn_83format, 0);
            else
                jsonfil = kopen4load(addonjsonfn_standard, 0);

            if (jsonfil == buildvfs_kfd_invalid)
            {
                LOG_F(ERROR, "Could not find addon descriptor for package: '%s'", currentaddonfilename);
                a_packagecleanup(grpfileidx);
                continue;
            }

            int32_t len = kfilelength(jsonfil);
            char* jsonTextBuf = (char *)Xmalloc(len+1);
            jsonTextBuf[len] = '\0';

            if (kread_and_test(jsonfil, jsonTextBuf, len))
            {
                LOG_F(ERROR, "Failed to read addon descriptor for package: '%s'", currentaddonfilename);
                Xfree(jsonTextBuf);
                kclose(jsonfil);
                a_packagecleanup(grpfileidx);
                continue;
            }
            kclose(jsonfil);

            addonjson_t & ajson = madd.jsonDat;
            Bstrncpy(ajson.dataPath, filepath, BMAX_PATH);
            const bool parseResult = a_parseaddonjson(ctx, &ajson, "/", jsonTextBuf);
            Xfree(jsonTextBuf);

            if (parseResult)
            {
                LOG_F(ERROR, "Fatal errors found in addon descriptor in package: '%s'", currentaddonfilename);
                ajson.addonType = ATYPE_INVALID;
                a_packagecleanup(grpfileidx);
                continue;
            }

            a_packagecleanup(grpfileidx);
            a_updateaddonentryname(g_nummenuaddons);
            ++g_nummenuaddons;
        }

        fnlist_clearnames(fnlist);
    }

    return 0;
}

static int32_t LoadLocalSubfolderAddons(sjson_context* ctx, fnlist_t* fnlist, const char* addondir)
{
    // look for addon directories
    BUILDVFS_FIND_REC *rec;
    fnlist_getnames(fnlist, addondir, "*", 0, -1);
    sjson_reset_context(ctx);

    for (rec=fnlist->finddirs; rec; rec=rec->next)
    {
        // these aren't actually directories we want to consider
        if (!strcmp(rec->name, ".")) continue;
        if (!strcmp(rec->name, "..")) continue;

        currentaddonfilename = rec->name;

        char basepath[BMAX_PATH], jsonpath[BMAX_PATH];
        Bsnprintf(basepath, ARRAY_SIZE(basepath), "%s/%s", addondir, rec->name);

        menuaddon_t & madd = g_menuaddons[g_nummenuaddons];
        madd.loadType = LT_FOLDER;
        madd.loadOrderIndex = g_nummenuaddons;

        Bsnprintf(jsonpath, ARRAY_SIZE(jsonpath), "%s/%s/addon.json", addondir, rec->name);
        buildvfs_kfd jsonfil = kopen4loadfrommod(jsonpath, 0);
        if (jsonfil == buildvfs_kfd_invalid)
        {
            LOG_F(ERROR, "Could not find addon descriptor at: '%s'", jsonpath);
            continue;
        }

        int32_t len = kfilelength(jsonfil);
        char* jsonTextBuf = (char *)Xmalloc(len+1);
        jsonTextBuf[len] = '\0';

        if (kread_and_test(jsonfil, jsonTextBuf, len))
        {
            LOG_F(ERROR, "Failed to read addon descriptor at: '%s'", jsonpath);
            kclose(jsonfil);
            Xfree(jsonTextBuf);
            continue;
        }
        kclose(jsonfil);

        addonjson_t & ajson = madd.jsonDat;
        Bstrncpy(ajson.dataPath, basepath, BMAX_PATH);
        const bool parseResult = a_parseaddonjson(ctx, &ajson, basepath, jsonTextBuf);
        Xfree(jsonTextBuf);

        if (parseResult)
        {
            LOG_F(ERROR, "Fatal errors found in addon descriptor at: '%s'", jsonpath);
            ajson.addonType = ATYPE_INVALID;
            continue;
        }
        a_updateaddonentryname(g_nummenuaddons);
        ++g_nummenuaddons;
    }
    fnlist_clearnames(fnlist);

    return 0;
}

static int32_t LoadWorkshopAddons(sjson_context* ctx)
{
    // TODO
    UNREFERENCED_PARAMETER(ctx);
    return 0;
}

// Load addon information from the package and subfolder json files
// into the menu addon storage
int32_t ReadAddonPackageDescriptors(void)
{
    if (G_GetLogoFlags() & LOGO_NOADDONS)
        return 0;

    // TODO: Need to devise a way to remember addons
    // create space for all potentially valid addons
    int32_t maxaddons = CountPotentialAddons();
    if (maxaddons <= 0)
    {
        DLOG_F(INFO, "No custom addons detected, aborting.");
        a_free_menuaddons();
        return -1;
    }

    if (g_menuaddons == nullptr)
    {
        g_menuaddons = (menuaddon_t *)Xcalloc(maxaddons, sizeof(menuaddon_t));
    }
    else
    {
        g_menuaddons = (menuaddon_t *)Xrealloc(g_menuaddons, maxaddons * sizeof(menuaddon_t));

        for (int i = 0; i < maxaddons; i++)
            g_menuaddons[i].clear();
        g_nummenuaddons = 0;
    }

    sjson_context * ctx = sjson_create_context(0, 0, nullptr);
    char * addondir = a_getaddondir();
    if (addondir)
    {
        fnlist_t fnlist = FNLIST_INITIALIZER;
        fnlist_clearnames(&fnlist);
        LoadLocalPackagedAddons(ctx, &fnlist, addondir);
        LoadLocalSubfolderAddons(ctx, &fnlist, addondir);
        Xfree(addondir);
    }
    LoadWorkshopAddons(ctx);
    sjson_destroy_context(ctx);

    if (g_nummenuaddons <= 0)
    {
        a_free_menuaddons();
        return -1;
    }

    g_menuaddons = (menuaddon_t *)Xrealloc(g_menuaddons, sizeof(menuaddon_t) * g_nummenuaddons);
    return 0;
}

int32_t LoadAddonPreviewImage(addonjson_t* mjsonStore)
{
    if (!mjsonStore->imageBuffer[0])
        return -1;

    walock[TILE_ADDONSHOT] = CACHE1D_PERMANENT;

    if (waloff[TILE_ADDONSHOT] == 0)
        g_cache.allocateBlock(&waloff[TILE_ADDONSHOT], PREVIEWTILEX * PREVIEWTILEY, &walock[TILE_ADDONSHOT]);

    tilesiz[TILE_ADDONSHOT].x = PREVIEWTILEX;
    tilesiz[TILE_ADDONSHOT].y = PREVIEWTILEY;

    Bmemcpy((char *)waloff[TILE_ADDONSHOT], mjsonStore->imageBuffer, PREVIEWTILEX * PREVIEWTILEY);
    tileInvalidate(TILE_ADDONSHOT, 0, 255);
    return 0;
}


// This function serves to clean up messy load order sequences, such that there are no gaps or duplicates
// e.g.: {2, 5, 4, 8, 4}
// turned into: {1, 3, 2, 4, 5}
void CleanUpLoadOrder()
{
    if (g_nummenuaddons <= 0 || !g_menuaddons)
        return;

    // get max load order
    int32_t i, cl, maxBufSize, maxLoadOrder = 0;
    for (i = 0; i < g_nummenuaddons; i++)
    {
        cl = g_menuaddons[i].loadOrderIndex + 1;
        if (cl > maxLoadOrder)
            maxLoadOrder = cl;
    }

    // allocate enough space for the case where all load order indices are duplicates
    maxBufSize = maxLoadOrder + g_nummenuaddons - 1;
    menuaddon_t** lobuf = (menuaddon_t**) Xcalloc(maxBufSize, sizeof(menuaddon_t*));

    // place pointers to menu addons corresponding to load order
    for (i = 0; i < g_nummenuaddons; i++)
    {
        cl = g_menuaddons[i].loadOrderIndex;
        if (EDUKE32_PREDICT_TRUE(!lobuf[cl]))
            lobuf[cl] = &g_menuaddons[i];
        else
        {
            // somehow had a duplicate load order index
            lobuf[maxLoadOrder++] = &g_menuaddons[i];
        }
    }

    // clean up load order
    int16_t newlo = 0;
    for (i = 0; i < maxLoadOrder; i++)
    {
        if (lobuf[i])
        {
            lobuf[i]->loadOrderIndex = newlo;
            newlo++;
        }
    }
    Xfree(lobuf);
}

// switch load order between two addon items, and update name
void SwapLoadOrder(int32_t indexA, int32_t indexB)
{
    int temp = g_menuaddons[indexA].loadOrderIndex;
    g_menuaddons[indexA].loadOrderIndex = g_menuaddons[indexB].loadOrderIndex;
    g_menuaddons[indexB].loadOrderIndex = temp;

    a_updateaddonentryname(indexA);
    a_updateaddonentryname(indexB);
}

int32_t PrepareSelectedAddon(menuaddon_t* seladdon)
{
    addonjson_t & seljson = seladdon->jsonDat;
    switch (seladdon->loadType)
    {
        case LT_FOLDER:
        {
            int32_t status = addsearchpath(seljson.dataPath);
            DLOG_F(INFO, "Result of trying to add '%s': %d", seljson.dataPath, status);
        }
            break;
        case LT_ZIP:
        case LT_SSI:
        case LT_GRP:
            G_AddGroup(seljson.dataPath);
            /*switch (seljson.addonType)
            {
                case ATYPE_MAIN:
                    DLOG_F(INFO, "Previous main grp: %s", g_grpNamePtr);
                    clearGrpNamePtr();
                    g_grpNamePtr = dup_filename(seljson.dataPath);
                    DLOG_F(INFO, "New main grp: %s", g_grpNamePtr);
                    break;
                case ATYPE_MODULE:
                    G_AddGroup(seljson.dataPath);
                    break;
                default:
                    return -1;
            }*/
            break;
        default:
            return -1;
    }

    if (seljson.scriptNamePath[0])
        G_AddCon(seljson.scriptNamePath);

    if (seljson.defNamePath[0])
        G_AddDef(seljson.defNamePath);

    return 0;
}

int32_t StartSelectedAddons(void)
{
    // addsearchpath(g_rootDir);
    return 0;
}
