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

// supported extensions
static const char grp_ext[] = "*.grp";
static const char ssi_ext[] = "*.ssi";
static const char* addon_extensions[] = { grp_ext, ssi_ext, "*.zip", "*.pk3", "*.pk4" };

// keys used in the JSON addon descriptor
static const char jsonkey_game[] = "game";
static const char jsonkey_title[] = "title";
static const char jsonkey_author[] = "author";
static const char jsonkey_version[] = "version";
static const char jsonkey_desc[] = "description";
static const char jsonkey_image[] = "image";
static const char jsonkey_con[] = "CON";
static const char jsonkey_def[] = "DEF";
static const char jsonkey_rts[] = "RTS";
static const char jsonkey_scripttype[] = "type";
static const char jsonkey_scriptpath[] = "path";

// json gametype values
static const char jsonval_gt_any[] = "any";
static const char jsonval_gt_duke[] = "duke3d";
static const char jsonval_gt_nam[] = "nam";
static const char jsonval_gt_ww2gi[] = "ww2gi";
static const char jsonval_gt_nam_ww2gi[] = "nam_ww2gi";
static const char jsonval_gt_fury[] = "fury";

// script path values
static const char jsonval_scriptmain[] = "main";
static const char jsonval_scriptmodule[] = "module";

// default addon content
static const char default_title[] = "Unnamed Addon";
static const char default_author[] = "N/A";
static const char default_version[] = "N/A";
static const char default_description[] = "--Empty Description--";

// hashtable for images
hashtable_t h_addonpreviews = { 1024, NULL };
// hashtable_t h_addondescriptions = { 1024, NULL };

// extern vars, tracks the currently loaded addons
useraddon_t * g_useraddons = nullptr;
uint16_t g_numuseraddons = 0;

// local path for loading addons and json descriptor filenames
static const char addon_dir[] = "./addons";
static const char addonjsonfn[] = "addon.json";

// adjust for mod directory
static int32_t Addon_GetLocalDir(char * pathbuf, const int32_t buflen)
{
    if (g_modDir[0] != '/' || g_modDir[1] != 0)
        Bsnprintf(pathbuf, buflen, "%s/%s", g_modDir, addon_dir);
    else
        Bstrncpy(pathbuf, addon_dir, buflen);

    if (!buildvfs_isdir(pathbuf))
    {
        DLOG_F(INFO, "Addon path does not exist: '%s", pathbuf);
        return -1;
    }

    return 0;
}

static void freehashpreviewimage(const char *, intptr_t key)
{
    Xfree((void *)key);
}
void Addon_FreePreviewHashTable()
{
    hash_loop(&h_addonpreviews, freehashpreviewimage);
    hash_free(&h_addonpreviews);
}

// free individual addon struct memory
static void Addon_FreeAddonContents(useraddon_t * addon)
{
    if (addon->uniqueId)
        DO_FREE_AND_NULL(addon->uniqueId);

    if (addon->jsondat.description)
        DO_FREE_AND_NULL(addon->jsondat.description);

    if (addon->jsondat.script_modules)
    {
        for (int j = 0; j < addon->jsondat.num_script_modules; j++)
            Xfree(addon->jsondat.script_modules[j]);
        DO_FREE_AND_NULL(addon->jsondat.script_modules);
    }

    if (addon->jsondat.def_modules)
    {
        for (int j = 0; j < addon->jsondat.num_def_modules; j++)
            Xfree(addon->jsondat.def_modules[j]);
        DO_FREE_AND_NULL(addon->jsondat.def_modules);
    }
}

// free all addon storage
void Addon_FreeUserAddons(void)
{
    if (g_useraddons)
    {
        for (int i = 0; i < g_numuseraddons; i++)
            Addon_FreeAddonContents(&g_useraddons[i]);

        DO_FREE_AND_NULL(g_useraddons);
    }
    g_numuseraddons = 0;
}

static bool Addon_CheckFilePresence(const char* filepath)
{
    buildvfs_kfd jsonfil = kopen4loadfrommod(filepath, 0);
    bool loadsuccess = (jsonfil != buildvfs_kfd_invalid);
    kclose(jsonfil);
    return loadsuccess;
}

// This function copies the given string into the text buffer and adds linebreaks at appropriate locations.
// * lblen : maximum number of characters until linebreak forced
static int32_t Addon_Strncpy_TextWrap(char *dst, const char *src, int32_t const maxlen, int32_t const lblen)
{
    // indices and line length
    const int endbufspace = 8;
    int srcidx = 0, dstidx = 0, lastwsidx = 0;
    int currlinelength = 0;

    int32_t linecount = 1;
    while (src[srcidx] && (dstidx < maxlen - endbufspace))
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

    if (dstidx >= maxlen - endbufspace)
    {
        Bstrcpy(&dst[dstidx], " [...]");
    }
    else
    {
        dst[dstidx] = '\0';
    }

    return linecount;
}

static int32_t Addon_ParseJson_String(useraddon_t *addon, sjson_node *node, const char *key,
                                        char *dstbuf, int32_t const bufsize)
{
    sjson_node * ele = sjson_find_member_nocase(node, key);
    if (ele == nullptr)
    {
        dstbuf[0] = '\0';
        return 1;
    }

    if (ele->tag != SJSON_STRING)
    {
        LOG_F(WARNING, "Addon descriptor member '%s' of addon '%s' is not string typed!", key, addon->uniqueId);
        return -1;
    }

    Bstrncpy(dstbuf, ele->string_, bufsize);
    if (dstbuf[bufsize-1])
    {
        LOG_F(WARNING, "Member '%s' of addon '%s' exceeds maximum size of %d chars!", key, addon->uniqueId, bufsize);
        dstbuf[bufsize-1] = '\0';
    }
    return 0;
}

static void Addon_AllocateDefaultDescription(useraddon_t *addon)
{
    const int desclen = ARRAY_SIZE(default_description);
    addon->jsondat.description = (char *) Xmalloc(desclen);
    Bstrncpy(addon->jsondat.description, default_description, desclen);

    addon->jsondat.desc_len = desclen;
    addon->jsondat.desc_linecnt = 1;
}

static int32_t Addon_ParseJson_Description(useraddon_t *addon, sjson_node *node, const char *key)
{
    sjson_node * ele = sjson_find_member_nocase(node, key);
    if (ele == nullptr)
        return 1;

    if (ele->tag != SJSON_STRING)
    {
        LOG_F(WARNING, "Addon descriptor member '%s' of addon '%s' is not string typed!", key, addon->uniqueId);
        return -1;
    }

    // add some extra space for linebreaks inserted by textwrap
    int desclen = strlen(ele->string_) + 256;
    if (desclen > MAXADDONDESC)
    {
        LOG_F(WARNING, "Member '%s' of addon '%s' exceeds maximum size of %d chars!", key, addon->uniqueId, MAXADDONDESC);
        desclen = MAXADDONDESC;
    }

    addon->jsondat.description = (char *) Xmalloc(desclen);
    addon->jsondat.desc_linecnt = Addon_Strncpy_TextWrap(addon->jsondat.description, ele->string_, desclen, (FURY) ? 84 : 64);
    addon->jsondat.desc_len = desclen;

    return 0;
}

static int32_t Addon_ParseJson_Scripts(useraddon_t *addon, sjson_node* root, const char* key, const char* basepath,
                                        char* mainscriptbuf, char** & modulebuffers, int32_t & modulecount)
{
    char scriptbuf[BMAX_PATH];
    int32_t numValidChildren = 0;

    mainscriptbuf[0] = '\0';
    modulebuffers = nullptr;
    modulecount = 0;

    sjson_node * nodes = sjson_find_member_nocase(root, key);
    if (nodes == nullptr)
        return 1;

    if (nodes->tag != SJSON_ARRAY)
    {
        LOG_F(WARNING, "Content of member '%s' of addon '%s' is not an array!", key, addon->uniqueId);
        return -1;
    }

    int numchildren = sjson_child_count(nodes);
    modulebuffers = (char **) Xmalloc(numchildren * sizeof(char*));

    sjson_node *snode, *script_path, *script_type;
    sjson_foreach(snode, nodes)
    {
        if (snode->tag != SJSON_OBJECT)
        {
            LOG_F(WARNING, "Invalid json type in array of member '%s' of addon '%s'!", key, addon->uniqueId);
            continue;
        }

        script_path = sjson_find_member_nocase(snode, jsonkey_scriptpath);
        if (script_path == nullptr || script_path->tag != SJSON_STRING)
        {
            LOG_F(WARNING, "Script path missing or has invalid format in addon '%s'!", addon->uniqueId);
            continue;
        }

        Bsnprintf(scriptbuf, BMAX_PATH, "%s/%s", basepath, script_path->string_);
        if (!Addon_CheckFilePresence(scriptbuf))
        {
            LOG_F(WARNING, "Script file of addon '%s' at location '%s' does not exist!", addon->uniqueId, scriptbuf);
            continue;
        }

        script_type = sjson_find_member_nocase(snode, jsonkey_scripttype);
        if (script_type == nullptr || script_type->tag != SJSON_STRING)
        {
            LOG_F(WARNING, "Script type missing or has invalid format in addon '%s'!", addon->uniqueId);
            continue;
        }

        if (!Bstrncasecmp(script_type->string_, jsonval_scriptmain, ARRAY_SIZE(jsonval_scriptmain)))
        {
            Bstrncpyz(mainscriptbuf, script_path->string_, BMAX_PATH);
        }
        else if (!Bstrncasecmp(script_type->string_, jsonval_scriptmodule, ARRAY_SIZE(jsonval_scriptmodule)))
        {
            modulebuffers[numValidChildren] = (char *) Xmalloc(BMAX_PATH);
            Bstrncpyz(modulebuffers[numValidChildren], script_path->string_, BMAX_PATH);
            numValidChildren++;
        }
        else
        {
            LOG_F(WARNING, "Invalid script type '%s' specified in addon '%s'!", script_type->string_, addon->uniqueId);
            continue;
        }
    }

    if (numValidChildren <= 0)
    {
        DO_FREE_AND_NULL(modulebuffers);
        modulecount = 0;
    }
    else
    {
        modulebuffers = (char **) Xrealloc(modulebuffers, numValidChildren * sizeof(char*));
        modulecount = numValidChildren;
    }
    return 0;
}


static int32_t Addon_LoadPreviewFromFile(char const *fn, uint8_t *imagebuffer)
{
#ifdef WITHKPLIB
    int32_t i, j, xsiz = 0, ysiz = 0;
    palette_t *picptr = NULL;

    kpzdecode(kpzbufload(fn), (intptr_t *)&picptr, &xsiz, &ysiz);
    if (xsiz != PREVIEWTILE_XSIZE || ysiz != PREVIEWTILE_YSIZE)
    {
        if (picptr) Xfree(picptr);
        LOG_F(WARNING, "Addon preview image '%s' does not have required format: %dx%d", fn, PREVIEWTILE_XSIZE, PREVIEWTILE_YSIZE);
        return -2;
    }

    if (!(paletteloaded & PALETTE_MAIN))
    {
        if (picptr) Xfree(picptr);
        LOG_F(WARNING, "Addon Preview: no palette loaded");
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

static int32_t Addon_ParseJson_PreviewImage(useraddon_t* addon, sjson_node* root, const char* key, const char* basepath)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    if (ele == nullptr)
        return -1;

    if (ele->tag != SJSON_STRING)
    {
        LOG_F(WARNING, "Provided image path for addon '%s' is not a string!", addon->uniqueId);
        return -1;
    }

    char fnbuf[BMAX_PATH];
    Bsnprintf(fnbuf, BMAX_PATH, "%s/%s", basepath, ele->string_);

    intptr_t cachedImage = hash_find(&h_addonpreviews, fnbuf);
    if (cachedImage == -1)
    {
        if (!Addon_CheckFilePresence(fnbuf))
        {
            LOG_F(WARNING, "Preview image of addon '%s' at location '%s' does not exist!", addon->uniqueId, fnbuf);
            return -1;
        }

        addon->jsondat.image_data = (uint8_t *) Xmalloc(PREVIEWTILE_XSIZE * PREVIEWTILE_YSIZE * sizeof(uint8_t));
        if (Addon_LoadPreviewFromFile(fnbuf, addon->jsondat.image_data) < 0)
        {
            DO_FREE_AND_NULL(addon->jsondat.image_data);
            return -1;
        }
        hash_add(&h_addonpreviews, fnbuf, (intptr_t) addon->jsondat.image_data, 0);
    }
    else
    {
        addon->jsondat.image_data = (uint8_t*) cachedImage;
    }

    return 0;
}

static int32_t Addon_ParseJson_RTSPath(useraddon_t* addon, sjson_node* root, const char* key, const char* basepath)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    if (ele == nullptr)
        return -1;

    if (ele->tag != SJSON_STRING)
    {
        LOG_F(WARNING, "Provided RTS file path for addon '%s' is not a string!", addon->uniqueId);
        return -1;
    }

    char full_path_buf[BMAX_PATH];
    Bsnprintf(full_path_buf, BMAX_PATH, "%s/%s", basepath, ele->string_);
    if (!Addon_CheckFilePresence(full_path_buf))
    {
        LOG_F(WARNING, "RTS file for addon '%s' at location '%s' does not exist!", addon->uniqueId, full_path_buf);
        return -1;
    }

    Bstrncpyz(addon->jsondat.main_rts_path, ele->string_, BMAX_PATH);
    return 0;
}

static int32_t Addon_ParseJson_GameFlag(useraddon_t* addon, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    if (ele == nullptr)
        return -1;

    if (ele->tag != SJSON_STRING)
    {
        LOG_F(WARNING, "Provided game type of addon '%s' is not a string!", addon->uniqueId);
        return -1;
    }

    if (!Bstrncasecmp(ele->string_, jsonval_gt_any, ARRAY_SIZE(jsonval_gt_any)))
        addon->gametype = BASEGAME_ANY;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_duke, ARRAY_SIZE(jsonval_gt_duke)))
        addon->gametype = BASEGAME_DUKE;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_fury, ARRAY_SIZE(jsonval_gt_fury)))
        addon->gametype = BASEGAME_FURY;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_ww2gi, ARRAY_SIZE(jsonval_gt_ww2gi)))
        addon->gametype = BASEGAME_WW2GI;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_nam, ARRAY_SIZE(jsonval_gt_nam)))
        addon->gametype = BASEGAME_NAM;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_nam_ww2gi, ARRAY_SIZE(jsonval_gt_nam_ww2gi)))
        addon->gametype = BASEGAME_NAM_WW2GI;
    else
    {
        LOG_F(WARNING, "Provided game type '%s' of '%s' is invalid!", ele->string_, addon->uniqueId);
        return -1;
    }

    return 0;
}

// Load data from json file into addon -- assumes that addon ID has been defined!
static int32_t Addon_ParseJson(useraddon_t* addon, sjson_context* ctx, const char* basepath)
{
    char json_path[BMAX_PATH];
    Bsnprintf(json_path, BMAX_PATH, "%s/%s", basepath, addonjsonfn);

    buildvfs_kfd jsonfil = kopen4load(json_path, 0);
    if (jsonfil == buildvfs_kfd_invalid)
    {
        // reduce extension from ".json" to ".jso"
        json_path[strlen(json_path) - 1] = '\0';
        jsonfil = kopen4load(json_path, 0);
        if (jsonfil == buildvfs_kfd_invalid)
        {
            DLOG_F(ERROR, "Could not find addon descriptor '%s' for addon: '%s'", json_path, addon->uniqueId);
            return -1;
        }
    }

    int32_t len = kfilelength(jsonfil);
    char* jsonTextBuf = (char *)Xmalloc(len+1);
    jsonTextBuf[len] = '\0';

    if (kread_and_test(jsonfil, jsonTextBuf, len))
    {
        DLOG_F(ERROR, "Failed to read addon descriptor at: '%s'", json_path);
        Xfree(jsonTextBuf);
        kclose(jsonfil);
        return -1;
    }
    kclose(jsonfil);

    sjson_reset_context(ctx);
    if (!sjson_validate(ctx, jsonTextBuf))
    {
        DLOG_F(ERROR, "Invalid addon descriptor JSON structure for addon '%s'!", addon->uniqueId);
        return -1;
    }

    sjson_node * root = sjson_decode(ctx, jsonTextBuf);
    Xfree(jsonTextBuf);

    // base game type
    if (Addon_ParseJson_GameFlag(addon, root, jsonkey_game))
        addon->gametype = BASEGAME_ANY;

    // load visual descriptors
    if (Addon_ParseJson_String(addon, root, jsonkey_title, addon->jsondat.title, MAXADDONTITLE))
        Bstrncpy(addon->jsondat.title, default_title, ARRAY_SIZE(default_title));

    if (Addon_ParseJson_String(addon, root, jsonkey_author, addon->jsondat.author, MAXADDONAUTHOR))
        Bstrncpy(addon->jsondat.author, default_author, ARRAY_SIZE(default_author));

    if (Addon_ParseJson_String(addon, root, jsonkey_version, addon->jsondat.version, MAXADDONVERSION))
        Bstrncpy(addon->jsondat.version, default_version, ARRAY_SIZE(default_version));

    if (Addon_ParseJson_Description(addon, root, jsonkey_desc))
        Addon_AllocateDefaultDescription(addon);

    // Load addon preview image
    // TODO: could load a default image using the return value
    Addon_ParseJson_PreviewImage(addon, root, jsonkey_image, basepath);

    // CON script paths
    Addon_ParseJson_Scripts(addon, root, jsonkey_con, basepath, addon->jsondat.main_script_path,
                            addon->jsondat.script_modules, addon->jsondat.num_script_modules);

    // DEF script paths
    Addon_ParseJson_Scripts(addon, root, jsonkey_def, basepath, addon->jsondat.main_def_path,
                            addon->jsondat.def_modules, addon->jsondat.num_def_modules);

    // RTS file path
    Addon_ParseJson_RTSPath(addon, root, jsonkey_rts, basepath);

    return 0;
}

static void Addon_PackageCleanup(int32_t grpfileidx)
{
    if (grpfileidx < numgroupfiles)
        popgroupfile(); // remove grp/ssi
    else
        popgroupfromkzstack(); // remove zip
}

// Count the number of addons present in the local folder, and the workshop folders.
static int32_t Addon_CountPotentialAddons(void)
{
    int32_t numaddons = 0;
    char * addonpathbuf = (char*) Xmalloc(BMAX_PATH);

    if (!Addon_GetLocalDir(addonpathbuf, BMAX_PATH))
    {
        fnlist_t fnlist = FNLIST_INITIALIZER;
        fnlist_clearnames(&fnlist);

        // get packages in the local addon dir
        for (auto & ext : addon_extensions)
        {
            fnlist_getnames(&fnlist, addonpathbuf, ext, -1, 0);
            numaddons += fnlist.numfiles;
            fnlist_clearnames(&fnlist);
        }

        fnlist_getnames(&fnlist, addonpathbuf, "*", 0, -1);
        for (BUILDVFS_FIND_REC *rec = fnlist.finddirs; rec; rec=rec->next)
        {
            if (!strcmp(rec->name, ".")) continue;
            if (!strcmp(rec->name, "..")) continue;
            numaddons++;
        }
        fnlist_clearnames(&fnlist);
    }
    Xfree(addonpathbuf);

    // TODO: get number of workshop addon folders

    return numaddons;
}

static int32_t Addon_ReadLocalPackages(sjson_context* ctx, fnlist_t* fnlist, const char* addondir)
{
    // search for local addon packages
    for (auto & ext : addon_extensions)
    {
        BUILDVFS_FIND_REC *rec;
        fnlist_getnames(fnlist, addondir, ext, -1, 0);
        for (rec=fnlist->findfiles; rec; rec=rec->next)
        {
            char package_path[BMAX_PATH];
            Bsnprintf(package_path, BMAX_PATH, "%s/%s", addondir, rec->name);

            useraddon_t & addon = g_useraddons[g_numuseraddons];
            addon.uniqueId = Xstrdup(package_path);
            Bstrncpy(addon.data_path, package_path, BMAX_PATH);
            addon.loadorder_idx = -1;

            // set initial file type based on extension
            if (!Bstrcmp(ext, grp_ext))
                addon.loadtype = LT_GRP;
            else if (!Bstrcmp(ext, ssi_ext))
                addon.loadtype = LT_SSI;
            else
                addon.loadtype = LT_ZIP;

            // try to load the package and change the grp file idx
            const int32_t grpfileidx = initgroupfile(package_path);
            if (grpfileidx == -1)
            {
                DLOG_F(ERROR, "Failed to open addon package at '%s'", package_path);
                Addon_FreeAddonContents(&addon);
                addon.loadtype = LT_INVALID;
                continue;
            }
            else if (grpfileidx >= numgroupfiles)
            {
                // zip file renamed to grp
                addon.loadtype = LT_ZIP;
            }

            if (Addon_ParseJson(&addon, ctx, "/"))
            {
                Addon_FreeAddonContents(&addon);
                Addon_PackageCleanup(grpfileidx);
                addon.loadtype = LT_INVALID;
                continue;
            }

            Addon_PackageCleanup(grpfileidx);

            // clean up based on game type
            if (!(g_gameType & addon.gametype))
                continue;

            ++g_numuseraddons;
        }

        fnlist_clearnames(fnlist);
    }

    return 0;
}

static int32_t Addon_ReadSubfolderAddons(sjson_context* ctx, fnlist_t* fnlist, const char* addondir)
{
    // look for addon directories
    BUILDVFS_FIND_REC *rec;
    fnlist_getnames(fnlist, addondir, "*", 0, -1);
    for (rec=fnlist->finddirs; rec; rec=rec->next)
    {
        // these aren't actually directories we want to consider
        if (!strcmp(rec->name, ".")) continue;
        if (!strcmp(rec->name, "..")) continue;

        char basepath[BMAX_PATH];
        Bsnprintf(basepath, BMAX_PATH, "%s/%s", addondir, rec->name);

        useraddon_t & addon = g_useraddons[g_numuseraddons];
        addon.uniqueId = Xstrdup(basepath);
        Bstrncpy(addon.data_path, basepath, BMAX_PATH);
        addon.loadtype = LT_FOLDER;
        addon.loadorder_idx = -1;

        if (Addon_ParseJson(&addon, ctx, basepath))
        {
            Addon_FreeAddonContents(&addon);
            addon.loadtype = LT_INVALID;
            continue;
        }

        // clean up based on game type
        if (!(g_gameType & addon.gametype))
            continue;

        ++g_numuseraddons;
    }
    fnlist_clearnames(fnlist);

    return 0;
}

static int32_t Addon_LoadWorkshopAddons(sjson_context* ctx)
{
    // TODO
    UNREFERENCED_PARAMETER(ctx);
    return 0;
}

static int32_t Addon_InitLoadOrderFromConfig()
{
    if (g_numuseraddons <= 0 || !g_useraddons)
        return -1;

    int32_t i, cl, maxLoadOrder = 0;

    // temp: get max load order
    for (i = 0; i < g_numuseraddons; i++)
    {
        cl = g_useraddons[i].loadorder_idx + 1;
        if (cl > maxLoadOrder)
            maxLoadOrder = cl;
    }

    return maxLoadOrder;
}

static void Addon_InitializeLoadOrder()
{
    int32_t i, cl, maxBufSize;
    if (g_numuseraddons <= 0 || !g_useraddons)
        return;

    int maxLoadOrder = Addon_InitLoadOrderFromConfig();

    // allocate enough space for the case where all load order indices are duplicates
    maxBufSize = maxLoadOrder + g_numuseraddons;
    useraddon_t** lobuf = (useraddon_t**) Xcalloc(maxBufSize, sizeof(useraddon_t*));

    // place pointers to menu addons corresponding to load order
    for (i = 0; i < g_numuseraddons; i++)
    {
        cl = g_useraddons[i].loadorder_idx;

        if (cl < 0 || lobuf[cl])
            lobuf[maxLoadOrder++] = &g_useraddons[i];
        else
            lobuf[cl] = &g_useraddons[i];
    }

    // clean up load order
    int16_t newlo = 0;
    for (i = 0; i < maxLoadOrder; i++)
    {
        if (lobuf[i])
        {
            lobuf[i]->loadorder_idx = newlo;
            lobuf[i]->updateMenuEntryName();
            newlo++;
        }
    }
    Xfree(lobuf);
}

int32_t Addon_ReadPackageDescriptors(void)
{
    if (G_GetLogoFlags() & LOGO_NOADDONS)
        return 0;

    // initialize hash table if it doesn't exist yet
    if (!h_addonpreviews.items)
        hash_init(&h_addonpreviews);

    // free current storage (large data)
    Addon_FreeUserAddons();

    // create space for all potentially valid addons
    int32_t maxaddons = Addon_CountPotentialAddons();
    if (maxaddons <= 0)
    {
        DLOG_F(INFO, "No custom addons detected.");
        return -1;
    }
    g_useraddons = (useraddon_t *)Xcalloc(maxaddons, sizeof(useraddon_t));

    sjson_context * ctx = sjson_create_context(0, 0, nullptr);
    char * addonpathbuf = (char*) Xmalloc(BMAX_PATH);
    if (!Addon_GetLocalDir(addonpathbuf, BMAX_PATH))
    {
        fnlist_t fnlist = FNLIST_INITIALIZER;
        fnlist_clearnames(&fnlist);
        Addon_ReadLocalPackages(ctx, &fnlist, addonpathbuf);
        Addon_ReadSubfolderAddons(ctx, &fnlist, addonpathbuf);
    }
    Xfree(addonpathbuf);

    Addon_LoadWorkshopAddons(ctx);
    sjson_destroy_context(ctx);

    if (g_numuseraddons <= 0)
    {
        DLOG_F(INFO, "No valid addons found.");
        Addon_FreeUserAddons();
        return -1;
    }

    g_useraddons = (useraddon_t *)Xrealloc(g_useraddons, sizeof(useraddon_t) * g_numuseraddons);
    Addon_InitializeLoadOrder();

    return 0;
}

int32_t Addon_LoadPreviewTile(addonjson_t* mjsonStore)
{
    if (!mjsonStore->image_data[0])
        return -1;

    walock[TILE_ADDONSHOT] = CACHE1D_PERMANENT;

    if (waloff[TILE_ADDONSHOT] == 0)
        g_cache.allocateBlock(&waloff[TILE_ADDONSHOT], PREVIEWTILE_XSIZE * PREVIEWTILE_YSIZE, &walock[TILE_ADDONSHOT]);

    tilesiz[TILE_ADDONSHOT].x = PREVIEWTILE_XSIZE;
    tilesiz[TILE_ADDONSHOT].y = PREVIEWTILE_YSIZE;

    Bmemcpy((char *)waloff[TILE_ADDONSHOT], mjsonStore->image_data, PREVIEWTILE_XSIZE * PREVIEWTILE_YSIZE);
    tileInvalidate(TILE_ADDONSHOT, 0, 255);
    return 0;
}

void Addon_SwapLoadOrder(int32_t indexA, int32_t indexB)
{
    useraddon_t & addonA = g_useraddons[indexA];
    useraddon_t & addonB = g_useraddons[indexB];

    int temp = addonA.loadorder_idx;
    addonA.loadorder_idx = addonB.loadorder_idx;
    addonB.loadorder_idx = temp;

    addonA.updateMenuEntryName();
    addonB.updateMenuEntryName();
}

int32_t Addon_PrepareSelectedAddon(useraddon_t* addon)
{
    switch (addon->loadtype)
    {
        case LT_FOLDER:
        {
            int32_t status = addsearchpath(addon->data_path);
            DLOG_F(INFO, "Result of trying to add '%s': %d", addon->data_path, status);
            break;
        }
        case LT_ZIP:
        case LT_SSI:
        case LT_GRP:
            G_AddGroup(addon->data_path);
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

    if (addon->jsondat.main_script_path[0])
        G_AddCon(addon->jsondat.main_script_path);

    if (addon->jsondat.main_def_path[0])
        G_AddDef(addon->jsondat.main_def_path);

    return 0;
}

int32_t Addon_StartSelectedAddons(void)
{
    // addsearchpath(g_rootDir);
    return 0;
}
