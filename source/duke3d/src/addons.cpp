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

#include "vfs.h"

static const char* a_currentaddonfilename;

static const char* a_addonextensions[] = { "*.grp", "*.zip", "*.ssi", "*.pk3", "*.pk4" };

static const char a_addonlocaldir[] = "/addons";
static const char a_addonjsonfn[] = "addon.json";
static const char a_addonjsonfn_grp[] = "addon.jso";

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
            if (dstidx - lastwsidx < lblen/4)
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
        LOG_F(ERROR, "Addon descriptor member '%s' of addon '%s' is not string typed!", key, a_currentaddonfilename);
        return -1;
    }

    if (lblen <= 0)
        Bstrncpy(dstbuf, ele->string_, bufsize);
    else
        *linecount = a_strncpy_textwrap(dstbuf, ele->string_, bufsize, lblen);

    if (dstbuf[bufsize-1])
    {
        LOG_F(WARNING, "Member '%s' of addon '%s' exceeds maximum size of %d chars!", key, a_currentaddonfilename, bufsize);
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
        LOG_F(ERROR, "Content of member '%s' of addon '%s' is not an array!", key, a_currentaddonfilename);
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
            LOG_F(ERROR, "Invalid json type in array of member '%s' of addon '%s'!", key, a_currentaddonfilename);
            continue;
        }

        sjson_node * script_path = sjson_find_member_nocase(snode, jsonkey_scriptpath);
        if (script_path == nullptr || script_path->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Script path missing or has invalid format in addon '%s'!", a_currentaddonfilename);
            continue;
        }

        Bsnprintf(scriptbuf, BMAX_PATH, "%s/%s", basepath, script_path->string_);
        if (!a_checkfilepresence(scriptbuf))
        {
            LOG_F(ERROR, "Script file of addon '%s' at location '%s' does not exist!", a_currentaddonfilename, scriptbuf);
            return -1;
        }

        sjson_node * script_type = sjson_find_member_nocase(snode, jsonkey_scripttype);
        if (script_type == nullptr || script_type->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Script type missing or has invalid format in addon '%s'!", a_currentaddonfilename);
            continue;
        }

        if (!Bstrncasecmp(script_type->string_, jsonvalue_addonmain, ARRAY_SIZE(jsonvalue_addonmain)))
        {
            Bstrncpy(mainscriptpath, scriptbuf, BMAX_PATH);
        }
        else if (!Bstrncasecmp(script_type->string_, jsonvalue_addonmodule, ARRAY_SIZE(jsonvalue_addonmodule)))
        {
            scriptmodules[numValidChildren] = (char *) Xmalloc(BMAX_PATH * sizeof(char));
            Bstrncpy(scriptmodules[numValidChildren], scriptbuf, BMAX_PATH);
            numValidChildren++;
        }
        else
        {
            LOG_F(ERROR, "Invalid script type '%s' specified in addon '%s'!", script_type->string_, a_currentaddonfilename);
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

// retrieve information from addon.json
// Returns 0 on success, -1 on error
static int32_t a_parseaddonjson(sjson_context* ctx, addonjson_t* mjsonStore, const char* basepath, char* raw_json)
{
    if (!sjson_validate(ctx, raw_json))
    {
        LOG_F(ERROR, "Invalid JSON structure for addon '%s'!", a_currentaddonfilename);
        return -1;
    }

    sjson_node * root = sjson_decode(ctx, raw_json);
    sjson_node * ele = nullptr;

    // data and image path
    Bstrncpy(mjsonStore->dataPath, basepath, BMAX_PATH);
    ele = sjson_find_member_nocase(root, jsonkey_image);
    if (ele != nullptr)
    {
        if (ele->tag == SJSON_STRING)
        {
            Bsnprintf(mjsonStore->imagePath, BMAX_PATH, "%s/%s", basepath, ele->string_);
            if (!a_checkfilepresence(mjsonStore->imagePath))
            {
                LOG_F(ERROR, "Preview image of addon '%s' at location '%s' does not exist!", a_currentaddonfilename, mjsonStore->imagePath);
                return -1;
            }
        }
        else
        {
            LOG_F(ERROR, "Image path for addon '%s' is not a string!", a_currentaddonfilename);
            return -1;
        }
    }

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
                LOG_F(ERROR, "Script file of addon '%s' at location '%s' does not exist!", a_currentaddonfilename, mjsonStore->rtsNamePath);
            }
        }
        else
        {
            LOG_F(ERROR, "Dependency must be an integer or a predefined string!");
        }
    }

    return 0;
}

// Count the number of addons present in the local folder, and the workshop folders.
static int32_t a_countaddonpackages(void)
{
    int32_t numaddons = 0;

    fnlist_t fnlist = FNLIST_INITIALIZER;
    fnlist_clearnames(&fnlist);
#if 0
    // get packages in the local addon dir
    for (auto & ext : a_addonpackexts)
    {
        fnlist_getnames(&fnlist, addon_localdir, ext, -1, 0);
        numaddons += fnlist.numfiles;
        fnlist_clearnames(&fnlist);
    }
#endif

    // get subfolders in the local addon dir
    fnlist_getnames(&fnlist, a_addonlocaldir, "*", 0, -1);
    numaddons += fnlist.numdirs;
    fnlist_clearnames(&fnlist);

    // TODO: get number of workshop addon folders

    return numaddons;
}

// Load addon information from the package and subfolder json files
// into the menu addon storage
void ReadAddonPackageDescriptors(void)
{
    int32_t potentialaddoncount = a_countaddonpackages();
    if (g_menuaddons == nullptr)
    {
        g_menuaddons = (menuaddon_t *)Xcalloc(potentialaddoncount, sizeof(menuaddon_t));
    }
    else
    {
        g_menuaddons = (menuaddon_t *)Xrealloc(g_menuaddons, potentialaddoncount * sizeof(menuaddon_t));

        for (int i = 0; i < potentialaddoncount; i++)
            g_menuaddons[i].clear();
        g_nummenuaddons = 0;
    }

    sjson_context * ctx = sjson_create_context(0, 0, nullptr);
    fnlist_t fnlist = FNLIST_INITIALIZER;
    fnlist_clearnames(&fnlist);

    // look for addon packages
    for (auto & ext : a_addonextensions)
    {
#if 0
        BUILDVFS_FIND_REC *rec;
        fnlist_getnames(&fnlist, addon_localdir, ext, -1, 0);

        for (rec=fnlist.findfiles; rec; rec=rec->next)
        {
            a_currentaddonfilename = rec->name;

            //Bsnprintf(pathbuf, sizeof(pathbuf), "%s/%s", addonpath, rec->name);
            //LOG_F(INFO, "Using group file %s", pathbuf);
            initgroupfile(pathbuf);

            // load the json
            char const * fn = rec->name;
            buildvfs_kfd fil = kopen4loadfrommod(fn, 0);
            if (fil == buildvfs_kfd_invalid)
                continue;
            kclose(fil);

            // TODO: Assign full data path from loaded
            menuaddon_t & madd = g_internaladdons[g_numinternaladdons];
            strncpy(madd.jsonDat.dataPath, fn, ARRAY_SIZE(madd.jsonDat.dataPath));
            ++g_numinternaladdons;

            // TODO: If name not in JSON, assign filename
            memcpy(madd.jsonDat.title, fn, ARRAY_SIZE(madd.jsonDat.title));
            popgroupfile();

            if (madd.isValid())
            {
                ++g_nummenuaddons;
            }
        }
        fnlist_clearnames(&fnlist);
#endif
    }

    // look for addon directories
    BUILDVFS_FIND_REC *rec;
    fnlist_getnames(&fnlist, a_addonlocaldir, "*", 0, -1);

    for (rec=fnlist.finddirs; rec; rec=rec->next)
    {
        // these aren't actually directories we want to consider
        if (!strcmp(rec->name, ".")) continue;
        if (!strcmp(rec->name, "..")) continue;

        a_currentaddonfilename = rec->name;

        char basepath[BMAX_PATH], jsonpath[BMAX_PATH];
        Bsnprintf(basepath, ARRAY_SIZE(basepath), "%s/%s", a_addonlocaldir, rec->name);

        menuaddon_t & madd = g_menuaddons[g_nummenuaddons];
        madd.loadType = LT_FOLDER;
        madd.loadOrderIndex = g_nummenuaddons;

        Bsnprintf(jsonpath, ARRAY_SIZE(jsonpath), "%s/%s/addon.json", a_addonlocaldir, rec->name);
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
        sjson_reset_context(ctx);
        const bool parseResult = a_parseaddonjson(ctx, &ajson, basepath, jsonTextBuf);
        Xfree(jsonTextBuf);

        if (parseResult)
        {
            LOG_F(ERROR, "Fatal errors found in addon descriptor at: '%s'", jsonpath);
            ajson.addonType = ATYPE_INVALID;
            continue;
        }

        ++g_nummenuaddons;
    }
    fnlist_clearnames(&fnlist);

    // TODO: Workshop directories

    sjson_destroy_context(ctx);
    g_menuaddons = (menuaddon_t *)Xrealloc(g_menuaddons, sizeof(menuaddon_t) * g_nummenuaddons);
}

int32_t G_LoadAddonPreviewImage(addonjson_t* mjsonStore)
{
    if (mjsonStore->imageBuffer[0])
    {
        DLOG_F(INFO, "Loading preview image from buffer.");
        walock[TILE_ADDONSHOT] = CACHE1D_PERMANENT;
        if (waloff[TILE_ADDONSHOT] == 0)
            g_cache.allocateBlock(&waloff[TILE_ADDONSHOT], PREVIEWTILEX * PREVIEWTILEY, &walock[TILE_ADDONSHOT]);
        tilesiz[TILE_ADDONSHOT].x = PREVIEWTILEX;
        tilesiz[TILE_ADDONSHOT].y = PREVIEWTILEY;
        Bmemcpy((char *)waloff[TILE_ADDONSHOT], mjsonStore->imageBuffer, PREVIEWTILEX * PREVIEWTILEY);
        tileInvalidate(TILE_ADDONSHOT, 0, 255);
        return 0;
    }
    else
    {
        const char* fn = mjsonStore->imagePath;
        DLOG_F(INFO, "Loading preview image at: '%s'", fn);
        buildvfs_kfd fil = kopen4loadfrommod(fn, 0);
        if ((fil == buildvfs_kfd_invalid) || loadtilefromfile(fn, TILE_ADDONSHOT, 255, 0))
        {
            LOG_F(ERROR, "Unable to load addon preview image at: '%s'", fn);
            mjsonStore->invalidImage = true;
            kclose(fil);
            return -1;
        }
        kclose(fil);

        walock[TILE_ADDONSHOT] = CACHE1D_PERMANENT;
        tileLoad(TILE_ADDONSHOT);

        if (tilesiz[TILE_ADDONSHOT].x != PREVIEWTILEX || tilesiz[TILE_ADDONSHOT].y != PREVIEWTILEY)
        {
            LOG_F(ERROR, "Addon preview image '%s' does not have resolution %dx%d", fn, PREVIEWTILEX, PREVIEWTILEY);
            if (waloff[TILE_ADDONSHOT] != 0)
                Bmemset((char *)waloff[TILE_ADDONSHOT], 0, tilesiz[TILE_ADDONSHOT].x * tilesiz[TILE_ADDONSHOT].y);
            waloff[TILE_ADDONSHOT] = 0;
            tileInvalidate(TILE_ADDONSHOT, 0, 255);
            mjsonStore->invalidImage = true;
            return -1;
        }

        Bmemcpy(mjsonStore->imageBuffer, (char *)waloff[TILE_ADDONSHOT], PREVIEWTILEX * PREVIEWTILEY);
        tileInvalidate(TILE_ADDONSHOT, 0, 255);

        return 0;
    }
}
