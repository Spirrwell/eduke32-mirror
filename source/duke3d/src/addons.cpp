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
#include "config.h"
#include "sjson.h"
#include "colmatch.h"
#include "kplib.h"
#include "vfs.h"

// cache for preview images, as palette conversion is slow
static hashtable_t h_addonpreviews = { MAXUSERADDONS, NULL };
static hashtable_t h_addontemp = { MAXUSERADDONS, NULL };

// supported extensions
static const char grp_ext[] = "*.grp";
static const char ssi_ext[] = "*.ssi";
static const char* addon_extensions[] = { grp_ext, ssi_ext, "*.zip", "*.pk3", "*.pk4" };

// keys used in the JSON addon descriptor
static const char jsonkey_depid[] = "id";
static const char jsonkey_game[] = "game";
static const char jsonkey_gamecrc[] = "gamecrc";
static const char jsonkey_version[] = "version";
static const char jsonkey_title[] = "title";
static const char jsonkey_author[] = "author";
static const char jsonkey_desc[] = "description";
static const char jsonkey_image[] = "preview";
static const char jsonkey_con[] = "CON";
static const char jsonkey_def[] = "DEF";
static const char jsonkey_rts[] = "RTS";
static const char jsonkey_scripttype[] = "type";
static const char jsonkey_scriptpath[] = "path";
static const char jsonkey_dependencies[] = "dependencies";
static const char jsonkey_incompatibles[] = "incompatibles";
static const char jsonkey_rendmode[] = "rendmode";
static const char jsonkey_startmap[] = "startmap";

// string sequences to identify different gametypes
static const char jsonval_gt_any[] = "any";
static const char jsonval_gt_duke[] = "duke3d";
static const char jsonval_gt_nam[] = "nam";
static const char jsonval_gt_ww2gi[] = "ww2gi";
static const char jsonval_gt_fury[] = "fury";

// string sequences to identify script type
static const char jsonval_scriptmain[] = "main";
static const char jsonval_scriptmodule[] = "module";

// map start keys
static const char jsonkey_mapvolume[] = "volume";
static const char jsonkey_maplevel[] = "level";
static const char jsonkey_mapfile[] = "file";

// rendmodes
static const char jsonval_rendmode_classic[] = "classic";
static const char jsonval_rendmode_opengl[] = "opengl";
static const char jsonval_rendmode_polymost[] = "polymost";
static const char jsonval_rendmode_polymer[] = "polymer";

// default addon strings
static const char missing_author[] = "N/A";
static const char missing_description[] = "No description available.";
static const char grpinfo_description[] = "Imported from grpinfo.";

// descriptions for hardcoded addons (taken from the back of the box or READMEs and adapted)
static const char dukevaca_description[] = "Ahhh... the Caribbean, the ultimate vacation destination. After a few months of alien annihilation, Duke's ready for a little R&R. Cabana girls, a beach-side bar and bermuda shorts are all he needs. That is, until the alien scum drop in for a little vacation of their own...";
static const char dukedc_description[] = "Aliens have captured the President! Duke gets word that alien scum have landed in Washington D.C., laid it to waste, and imprisoned the leader of the free world. Always up for a heroic deed, Duke heads to D.C. to rid the city of enemy dirtbags and return the president to power!";
static const char dukenw_description[] = "There's diabolical danger in the northern Ice-Land. Alien scum have taken over, and the fate of everyone's favorite jolly old man and his village of merry little ones hinges on an icy rescue. The Winter Wonderland will never be the same once Duke's begun the Arctic Meltdown.";
static const char dukezone_description[] = "Features 3 new episodes that contain 7 levels each. These maps take Duke across urban arctic wastelands, underground passages, canyons, fun houses, bars and a toxic chemical processing plant. Does not include the 500 levels packaged with the original release of the addon.";
static const char dukepentp_description[] = "Set between the third and fourth episode of Duke Nukem 3D. While Duke was trying to establish a little \"beach-head,\" the aliens have dropped in to break up his fun in the sun and spoil a couple of Penthouse photo shoots to boot. It's up to Duke Nukem to save the day - again.";

// dependency IDs for hardoced addons
static const char dukevaca_id[] = "dukevaca";
static const char dukedc_id[] = "dukedc";
static const char dukenw_id[] = "dukenw";
static const char dukezone_id[] = "dukezone";
static const char dukepentp_id[] = "dukepentp";

// authors of the hardcoded addons
static const char author_sunstorm[] = "Sunstorm Interactive";
static const char author_sillysoft[] = "Simply Silly Software";
static const char author_intersphere[] = "Intersphere Communications, Ltd. and Tyler Matthews";

// static addon tracker, only used temporarily before addons are separated into categories
static useraddon_t** s_useraddons = nullptr;
static int32_t s_numuseraddons = 0;

// folder name and json descriptor filename
static const char addon_dir[] = "addons";
static const char addonjsonfn[] = "addon.json";

// extern variables
useraddon_t** g_useraddons_grpinfo = nullptr;
useraddon_t** g_useraddons_tcs = nullptr;
useraddon_t** g_useraddons_mods = nullptr;

int32_t g_addoncount_grpinfo = 0;
int32_t g_addoncount_tcs = 0;
int32_t g_addoncount_mods = 0;

int32_t g_num_selected_addons = 0;
int32_t g_num_active_mdeps = 0;
int32_t g_num_active_incompats = 0;

int32_t g_addon_selrendmode = ADDON_RENDNONE;

bool g_addon_failedboot = false;

// shorthands for common iteration types
#define ITER_GRPINFO(_idx, _ptr) for (_idx = 0, _ptr = (g_addoncount_grpinfo > 0 ? g_useraddons_grpinfo[0] : nullptr);\
                                    _idx < g_addoncount_grpinfo; _ptr = g_useraddons_grpinfo[min(++_idx, g_addoncount_grpinfo-1)])

#define ITER_TCS(_idx, _ptr) for (_idx = 0, _ptr = (g_addoncount_tcs > 0 ? g_useraddons_tcs[0] : nullptr);\
                                     _idx < g_addoncount_tcs; _ptr = g_useraddons_tcs[min(++_idx, g_addoncount_tcs-1)])

#define ITER_MODS(_idx, _ptr) for (_idx = 0, _ptr = (g_addoncount_mods > 0 ? g_useraddons_mods[0] : nullptr);\
                                     _idx < g_addoncount_mods; _ptr = g_useraddons_mods[min(++_idx, g_addoncount_mods-1)])


// Check if the addon directory exists. This is always placed in the folder where the exe is found.
static int32_t Addon_GetLocalDir(char * pathbuf, const int32_t buflen)
{
    char* appdir = Bgetappdir();
    Bsnprintf(pathbuf, buflen, "%s/%s", appdir, addon_dir);
    Xfree(appdir);

    if (!buildvfs_isdir(pathbuf))
    {
        // DLOG_F(INFO, "Addon path does not exist: '%s", pathbuf);
        return -1;
    }

    return 0;
}

// free individual addon struct memory
static void Addon_FreeAddonContents(useraddon_t * addonPtr)
{
    if (addonPtr->internalId)
        DO_FREE_AND_NULL(addonPtr->internalId);

    if (addonPtr->jsondat.script_modules)
    {
        for (int j = 0; j < addonPtr->jsondat.num_script_modules; j++)
            Xfree(addonPtr->jsondat.script_modules[j]);
        DO_FREE_AND_NULL(addonPtr->jsondat.script_modules);
    }
    addonPtr->jsondat.num_script_modules = 0;

    if (addonPtr->jsondat.def_modules)
    {
        for (int j = 0; j < addonPtr->jsondat.num_def_modules; j++)
            Xfree(addonPtr->jsondat.def_modules[j]);
        DO_FREE_AND_NULL(addonPtr->jsondat.def_modules);
    }
    addonPtr->jsondat.num_def_modules = 0;

    if (addonPtr->jsondat.dependencies)
        DO_FREE_AND_NULL(addonPtr->jsondat.dependencies);
    addonPtr->jsondat.num_dependencies = 0;

    if (addonPtr->jsondat.incompatibles)
        DO_FREE_AND_NULL(addonPtr->jsondat.incompatibles);
    addonPtr->jsondat.num_incompatibles = 0;
}

// free all addons of given array
static void Addon_FreeUserAddonsForStruct(useraddon_t** & useraddons, int32_t & addoncount)
{
    if (useraddons)
    {
        for (int i = 0; i < addoncount; i++)
        {
            if (useraddons[i])
            {
                Addon_FreeAddonContents(useraddons[i]);
                Xfree(useraddons[i]);
            }
        }
        DO_FREE_AND_NULL(useraddons);
    }
    addoncount = 0;
}

// utility to check for file existence
static int32_t Addon_CheckFilePresence(const char* filepath)
{
    buildvfs_kfd jsonfil = kopen4load(filepath, 0);
    if (jsonfil != buildvfs_kfd_invalid)
    {
        kclose(jsonfil);
        return 0;
    }

    return -1;
}

// remove leading slashes and other non-alpha chars
static char* Addon_CreateInternalIdentity(const char* src, const int32_t srclen)
{
    int i = 0;
    while (i < srclen && !isalpha(src[i])) i++;
    return (i >= srclen) ? nullptr : Xstrdup(&src[i]);
}

// extract first segment of version and the starting index of the next segment
// also returns the char after the segment ends
// e.g. given string "24.0.1" it will extract 24 and return '.'
static char Addon_ParseVersionSegment(const char* vString, int32_t & segment, int32_t & nextSegmentStartIndex)
{
    // this function assumes that strings were previously verified with Addon_CheckVersionFormat()
    int k = 0; char c;
    while((c = vString[k++]))
    {
        nextSegmentStartIndex++;
        if (c == '.' || c == '-')
            break;
    }
    segment = Batoi(vString);
    return c;
}

// compares two version strings. Outputs 0 if equal, 1 if versionA is greater, -1 if versionB is greater.
static int32_t Addon_CompareVersionStrings(const char* versionA, const char* versionB)
{
    // this function assumes that strings were previously verified with Addon_CheckVersionFormat()
    char c1, c2;
    int v1, v2, i = 0, j = 0;
    while(versionA[i] && versionB[j])
    {
        c1 = Addon_ParseVersionSegment(&versionA[i], v1, i);
        c2 = Addon_ParseVersionSegment(&versionB[j], v2, j);

        // first check segment value
        if (v1 > v2)
            return 1;
        else if (v1 < v2)
            return -1;
        // char '.' is greater than '-' or null
        else if (c1 == '.' && (c2 == '\0' || c2 == '-'))
            return 1;
        else if ((c1 == '\0' || c1 == '-') && (c2 == '.'))
            return -1;
        // '-' is greater than null
        else if ((c1 == '-') && (c2 == '\0'))
            return 1;
        else if ((c1 == '\0') && (c2 == '-'))
            return -1;
        // if both null here, equivalent
        else if (c1 == '\0' && c2 == '\0')
            return 0;
        // if both '-', check ASCI ordering in remaining string
        else if (c1 == '-' && c2 == '-')
        {
            while (versionA[i] && versionB[j])
            {
                if (versionA[i] > versionB[j])
                    return 1;
                else if (versionA[i] < versionB[j])
                    return -1;
                i++; j++;
            }
        }
        // if both are '.', loop continues
    }

    // if we end up here, either versionA[i] or versionA[j] is null
    if (versionA[i] == versionB[j])
        return 0;
    else if (versionA[i] > versionB[j])
        return 1;
    else
        return -1;
}

// This awful function verifies that version strings adhere to the following format:
// REGEX: ((([1-9][0-9]*)|0)\.)*(([1-9][0-9]*))(\-[a-zA-Z0-9]+)?
// valid examples: {1.0, 1.0.3.4-alphanum123, 2, 4.0-a}
// This would be better accomplished with a regular expression library
static int32_t Addon_CheckVersionFormat(const char* versionString)
{
    if (!versionString || !versionString[0])
        return -1;

    int k = 0;
    char first, segChar = '\0';
    while ((first = versionString[k]) && segChar != '-')
    {
        // first char must be a digit and not a leading zero followed by a digit
        if (!isdigit(first) || (first == '0' && isdigit(versionString[k+1])))
        {
            // DLOG_F(WARNING, "Version grouping starts with invalid char: %c", first);
            return -1;
        }
        k++;

        // if non-null, go to next period/dash
        while ((segChar = versionString[k]))
        {
            k++;
            if (segChar == '.' || segChar == '-')
                break;

            if (!isdigit(segChar))
            {
                // DLOG_F(WARNING, "Non-digit found in version grouping: %c", curChar);
                return -1;
            }
        }
    }

    // must not end with period
    if (segChar == '.')
    {
        // DLOG_F(WARNING, "Version string cannot end with period char!");
        return -1;
    }

    // allow arbitrary alpha-numerical string after dash
    if (segChar == '-')
    {
        if (!versionString[k])
        {
            // DLOG_F(WARNING, "No characters following dash!");
            return -1;
        }

        while ((segChar = versionString[k++]))
        {
            if (!isalnum(segChar))
            {
                // DLOG_F(WARNING, "Non-alphanum char found after dash: %c", curChar);
                return -1;
            }
        }
    }

    return 0;
}

// utility to check if element is string typed
static int32_t Addon_CheckJsonStringType(const useraddon_t *addonPtr, sjson_node *ele, const char *key)
{
    if (ele->tag != SJSON_STRING)
    {
        LOG_F(ERROR, "Addon descriptor member '%s' of addon '%s' is not string typed!", key, addonPtr->internalId);
        return -1;
    }
    return 0;
}

static int32_t Addon_CheckIdentityFormat(const useraddon_t *addonPtr, const char* ident)
{
    if (!ident[0])
    {
        LOG_F(ERROR, "Identity string of addon %s cannot be empty!", addonPtr->internalId);
        return -1;
    }

    if (strlen(ident) >= ADDON_MAXID - 1)
    {
        LOG_F(ERROR, "Identity string of addon %s exceeds maximum size of %d chars!", addonPtr->internalId, ADDON_MAXID);
        return -1;
    }

    for (int i = 0; ident[i]; i++)
    {
        if (isspace(ident[i]))
        {
            LOG_F(ERROR, "Identity string of addon %s must not contain whitespace!", addonPtr->internalId);
            return -1;
        }
    }

    return 0;
}

// parse dependency identity
static int32_t Addon_ParseJson_Identity(useraddon_t *addonPtr, sjson_node *root, const char *key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->jsondat.externalId[0] = '\0';

    if (ele == nullptr) return 1;
    else if (Addon_CheckJsonStringType(addonPtr, ele, key) || Addon_CheckIdentityFormat(addonPtr, ele->string_))
        return -1;

    Bstrncpy(addonPtr->jsondat.externalId, ele->string_, ADDON_MAXID);
    return 0;
}

// parse arbitrary string
static int32_t Addon_ParseJson_String(useraddon_t *addonPtr, sjson_node *root, const char *key,
                                        char *dstbuf, int32_t const bufsize)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    dstbuf[0] = '\0';

    if (ele == nullptr) return 1;
    else if (Addon_CheckJsonStringType(addonPtr, ele, key)) return -1;

    Bstrncpy(dstbuf, ele->string_, bufsize);
    if (dstbuf[bufsize-1])
    {
        // non-fatal, just truncate
        LOG_F(WARNING, "Member '%s' of addon '%s' exceeds maximum size of %d chars!", key, addonPtr->internalId, bufsize);
        dstbuf[bufsize-1] = '\0';
    }

    return 0;
}

// interpret string as path and check file presence
static int32_t Addon_ParseJson_FilePath(useraddon_t* addonPtr, sjson_node* root, const char* key, char *dstbuf, const char* basepath)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    dstbuf[0] = '\0';

    if (ele == nullptr) return 1;
    else if (Addon_CheckJsonStringType(addonPtr, ele, key)) return -1;

    // if exceeds maxsize, file presence check will fail anyways
    Bsnprintf(dstbuf, BMAX_PATH, "%s/%s", basepath, ele->string_);
    if (Addon_CheckFilePresence(addonPtr->jsondat.preview_path))
    {
        LOG_F(ERROR, "File for key '%s' of addon '%s' at location '%s' does not exist!", key, addonPtr->internalId, dstbuf);
        dstbuf[0] = '\0';
        return -1;
    }

    Bcorrectfilename(dstbuf, 0);
    return 0;
}

// parse version and check its format
static int32_t Addon_ParseJson_Version(useraddon_t *addonPtr, sjson_node *root, const char *key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->jsondat.version[0] = '\0';

    if (ele == nullptr) return 1;
    else if (Addon_CheckJsonStringType(addonPtr, ele, key)) return -1;

    Bstrncpy(addonPtr->jsondat.version, ele->string_, ADDON_MAXVERSION);
    if (addonPtr->jsondat.version[ADDON_MAXVERSION-1])
    {
        LOG_F(ERROR, "Member '%s' of addon '%s' exceeds maximum size of %d chars!", key, addonPtr->internalId, ADDON_MAXVERSION);
        addonPtr->jsondat.version[0] = '\0';
        return -1;
    }

    if (Addon_CheckVersionFormat(addonPtr->jsondat.version))
    {
        LOG_F(ERROR, "Version string '%s' of addon %s has incorrect format!", addonPtr->jsondat.version, addonPtr->internalId);
        addonPtr->jsondat.version[0] = '\0';
        return -1;
    }

    return 0;
}

// retrieve the description -- in this case we allocate new memory, rather than just copying the string into an existing buffer
static int32_t Addon_ParseJson_Description(useraddon_t *addonPtr, sjson_node *root, const char *key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->jsondat.description = nullptr;

    if (ele == nullptr) return 1;
    else if (Addon_CheckJsonStringType(addonPtr, ele, key)) return -1;

    int desclen = strlen(ele->string_) + 1;
    if (desclen > ADDON_MAXDESC)
    {
        // non-fatal, just truncate
        LOG_F(WARNING, "Description of addon %s exceeds maximum of %d characters (dude, write your novel elsewhere)", addonPtr->internalId, ADDON_MAXDESC);
        desclen = ADDON_MAXDESC;
    }
    addonPtr->jsondat.description = (char*) Xmalloc(desclen);
    Bstrncpyz(addonPtr->jsondat.description, ele->string_, desclen);

    return 0;
}

// parse and store script file paths, check for file presence and other errors
static int32_t Addon_ParseJson_Scripts(useraddon_t *addonPtr, sjson_node* root, const char* key, const char* basepath,
                                        char* mainscriptbuf, char** & modulebuffers, int32_t & modulecount)
{
    char scriptbuf[BMAX_PATH];
    int32_t numValidChildren = 0;

    // by default, set to null
    mainscriptbuf[0] = '\0';
    modulebuffers = nullptr;
    modulecount = 0;

    sjson_node * nodes = sjson_find_member_nocase(root, key);
    if (nodes == nullptr)
        return 1;

    if (nodes->tag != SJSON_ARRAY)
    {
        LOG_F(ERROR, "Content of member '%s' of addon '%s' is not an array!", key, addonPtr->internalId);
        return -1;
    }

    int const numchildren = sjson_child_count(nodes);
    modulebuffers = (char **) Xmalloc(numchildren * sizeof(char*));

    bool hasError = false;
    sjson_node *snode, *script_path, *script_type;
    sjson_foreach(snode, nodes)
    {
        if (snode->tag != SJSON_OBJECT)
        {
            LOG_F(ERROR, "Invalid type found in array of member '%s' of addon '%s'!", key, addonPtr->internalId);
            hasError = true;
            continue;
        }

        script_path = sjson_find_member_nocase(snode, jsonkey_scriptpath);
        if (script_path == nullptr || script_path->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Script path missing or has invalid format in addon '%s'!", addonPtr->internalId);
            hasError = true;
            continue;
        }

        Bsnprintf(scriptbuf, BMAX_PATH, "%s/%s", basepath, script_path->string_);
        if (Addon_CheckFilePresence(scriptbuf))
        {
            LOG_F(ERROR, "Script file of addon '%s' at location '%s' does not exist!", addonPtr->internalId, scriptbuf);
            hasError = true;
            continue;
        }

        script_type = sjson_find_member_nocase(snode, jsonkey_scripttype);
        if (script_type == nullptr || script_type->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Script type missing or has invalid format in addon '%s'!", addonPtr->internalId);
            hasError = true;
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
            LOG_F(ERROR, "Invalid script type '%s' specified in addon '%s'!", script_type->string_, addonPtr->internalId);
            LOG_F(INFO, "Valid types are: {\"%s\", \"%s\"}", jsonval_scriptmain, jsonval_scriptmodule);
            hasError = true;
        }
    }

    // on error, abort and free valid items again
    if (hasError)
    {
        for (int i = 0; i < numValidChildren; i++)
            Xfree(modulebuffers[i]);
        numValidChildren = 0;
    }

    // valid children may be zero from error or no modules specified
    if (numValidChildren == 0)
    {
        DO_FREE_AND_NULL(modulebuffers);
        modulecount = 0;
        return (hasError) ? -1 : 0;
    }
    else
    {
        modulebuffers = (char **) Xrealloc(modulebuffers, numValidChildren * sizeof(char*));
        modulecount = numValidChildren;
        return 0;
    }
}

// the version string in the dependency portion is prepended with comparison characters
static int32_t Addon_SetupDependencyVersion(addondependency_t * dep, const char* versionString)
{
    if (versionString == nullptr || versionString[0] == '\0')
    {
        dep->version[0] = '\0'; dep->cOp = VCOMP_NOOP;
        return -1;
    }

    int k = 0;
    switch(versionString[0])
    {
        case '=':
            k++;
            if (versionString[1] == '=') { dep->cOp = VCOMP_EQ; k++; }
            else
            {
                LOG_F(ERROR, "Version string '%s' has incorrect format!", dep->version);
                dep->version[0] = '\0'; dep->cOp = VCOMP_NOOP;
                return -1;
            }
            break;
        case '>':
            k++;
            if (versionString[1] == '=') { dep->cOp = VCOMP_GTEQ; k++; }
            else dep->cOp = VCOMP_GT;
            break;
        case '<':
            k++; if (versionString[1] == '=') { dep->cOp = VCOMP_LTEQ; k++; }
            else dep->cOp = VCOMP_LT;
            break;
        default:
            // assume equality
            dep->cOp = VCOMP_EQ;
            break;
    }

    Bstrncpy(dep->version, &versionString[k], ADDON_MAXVERSION);
    if (dep->version[ADDON_MAXVERSION-1])
    {
        LOG_F(ERROR, "Dependency version '%s' exceeds maximum size of %d chars!", &versionString[k], ADDON_MAXVERSION);
        dep->version[0] = '\0'; dep->cOp = VCOMP_NOOP;
        return -1;
    }

    if (Addon_CheckVersionFormat(dep->version))
    {
        LOG_F(ERROR, "Version string '%s' has incorrect format!", dep->version);
        dep->version[0] = '\0'; dep->cOp = VCOMP_NOOP;
        return -1;
    }

    return 0;
}

// get the dependency property
static int32_t Addon_ParseJson_Dependency(useraddon_t* addonPtr, sjson_node* root, const char* key,
                                          addondependency_t*& dep_ptr, int32_t& num_valid_deps)
{
    sjson_node * nodes = sjson_find_member_nocase(root, key);
    if (nodes == nullptr) return 1;

    if (nodes->tag != SJSON_ARRAY)
    {
        LOG_F(ERROR, "Content of member '%s' of addon '%s' is not an array!", key, addonPtr->internalId);
        return -1;
    }

    int const numchildren = sjson_child_count(nodes);

    dep_ptr = (addondependency_t *) Xcalloc(numchildren, sizeof(addondependency_t));
    num_valid_deps = 0;

    sjson_node *snode;
    sjson_node *dep_uid, *dep_version;
    sjson_foreach(snode, nodes)
    {
        if (snode->tag != SJSON_OBJECT)
        {
            LOG_F(ERROR, "Invalid type found in array of member '%s' of addon '%s'!", key, addonPtr->internalId);
            continue;
        }

        dep_uid = sjson_find_member_nocase(snode, jsonkey_depid);
        if (dep_uid == nullptr || dep_uid->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Dependency Id in key '%s' is missing or has invalid format in addon '%s'!", key, addonPtr->internalId);
            continue;
        }

        dep_version = sjson_find_member_nocase(snode, jsonkey_version);
        if (dep_version != nullptr)
        {
            if (dep_version->tag != SJSON_STRING)
            {
                LOG_F(ERROR, "Dependency version %s in key '%s' is not a string in addon '%s'!", dep_uid->string_, key, addonPtr->internalId);
                continue;
            }
        }

        addondependency_t & adt = dep_ptr[num_valid_deps];
        adt.setFulfilled(false);

        // required checks on dependency Id
        if (Addon_CheckIdentityFormat(addonPtr, dep_uid->string_))
            continue;
        Bstrncpy(adt.depId, dep_uid->string_, ADDON_MAXID);

        // only bail if string specified and invalid, otherwise accept dependencies without version
        if (dep_version && Addon_SetupDependencyVersion(&adt, dep_version->string_))
        {
            LOG_F(ERROR, "Invalid version string for dependency '%s' in addon: %s!", adt.depId, addonPtr->internalId);
            continue;
        }

        num_valid_deps++;
    }

    if (num_valid_deps < numchildren)
    {
        DO_FREE_AND_NULL(dep_ptr);
        num_valid_deps = 0;
        return -1;
    }

    dep_ptr = (addondependency_t *) Xrealloc(dep_ptr, num_valid_deps * sizeof(addondependency_t));
    return 0;
}

// differs from the other functions in that it directly returns the gameflag value
static addongame_t Addon_ParseJson_GameFlag(const useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    if (ele == nullptr || Addon_CheckJsonStringType(addonPtr, ele, key))
        return BASEGAME_NONE;

    if (!Bstrncasecmp(ele->string_, jsonval_gt_any, ARRAY_SIZE(jsonval_gt_any)))
        return BASEGAME_ANY;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_duke, ARRAY_SIZE(jsonval_gt_duke)))
        return BASEGAME_DUKE;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_fury, ARRAY_SIZE(jsonval_gt_fury)))
        return BASEGAME_FURY;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_ww2gi, ARRAY_SIZE(jsonval_gt_ww2gi)))
        return BASEGAME_WW2GI;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_nam, ARRAY_SIZE(jsonval_gt_nam)))
        return BASEGAME_NAM;
    else
    {
        LOG_F(ERROR, "Invalid gametype on addon '%s'.\nValid gametype strings are: {%s, %s, %s, %s, %s}.",
                addonPtr->internalId, jsonval_gt_any, jsonval_gt_duke, jsonval_gt_nam, jsonval_gt_ww2gi, jsonval_gt_fury);
        return BASEGAME_NONE;
    }
}

// The gameCRC acts as an additional method to finegrain control for which game the addon should show up.
static int32_t Addon_ParseJson_GameCRC(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->gamecrc = 0;

    if (ele == nullptr) return 1;
    else if (Addon_CheckJsonStringType(addonPtr, ele, key)) return -1;

    if (ele->string_[0] != '0' || (ele->string_[1] != 'x' && ele->string_[1] != 'X'))
    {
        LOG_F(ERROR, "Missing hexadecimal prefix on '%s' for addon %s!", key, addonPtr->internalId);
        return -1;
    }

    char* endptr;
    int32_t hex = Bstrtol(ele->string_, &endptr, 0);
    if (hex == 0 || *endptr)
    {
        LOG_F(ERROR, "Value %s in addon %s is not a valid hexadecimal!", ele->string_, addonPtr->internalId);
        return -1;
    }

    addonPtr->gamecrc = hex;
    return 0;
}

// start map
static int32_t Addon_ParseJson_StartMap(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->jsondat.boardfilename[0] = '\0';
    addonPtr->jsondat.startlevel = 0;
    addonPtr->jsondat.startvolume = 0;
    addonPtr->flags &= ~ADDONFLAG_STARTMAP;

    if (ele == nullptr) return 1;
    else if (ele->tag != SJSON_OBJECT)
    {
        LOG_F(ERROR, "Value for key '%s' of addon %s must be an object!", key, addonPtr->internalId);
        return -1;
    }

    sjson_node* ele_mapfile = sjson_find_member_nocase(ele, jsonkey_mapfile);
    if (ele_mapfile)
    {
        if (Addon_CheckJsonStringType(addonPtr, ele_mapfile, jsonkey_mapfile))
            return -1;

        // do not check for file existence
        Bstrncpy(addonPtr->jsondat.boardfilename, ele_mapfile->string_, BMAX_PATH);
        Bcorrectfilename(addonPtr->jsondat.boardfilename, 0);
        addonPtr->jsondat.startlevel = 7;
        addonPtr->jsondat.startvolume = 0;
        addonPtr->flags |= ADDONFLAG_STARTMAP;
        return 0;
    }

    sjson_node* ele_maplevel = sjson_find_member_nocase(ele, jsonkey_maplevel);
    sjson_node* ele_mapvolume = sjson_find_member_nocase(ele, jsonkey_mapvolume);
    if (ele_maplevel && ele_mapvolume)
    {
        if (ele_maplevel->tag != SJSON_NUMBER || ele_mapvolume->tag != SJSON_NUMBER)
        {
            LOG_F(ERROR, "Level and volume are not integers in addon: %s!", addonPtr->internalId);
            return -1;
        }

        if ((ele_maplevel->number_ < 0 || ele_maplevel->number_ >= MAXLEVELS)
            || (ele_mapvolume->number_ < 0 || ele_mapvolume->number_ >= MAXVOLUMES))
        {
            LOG_F(ERROR, "Level or Volume exceed boundaries in addon: %s!", addonPtr->internalId);
            return -1;
        }

        addonPtr->jsondat.startlevel = ele_maplevel->number_;
        addonPtr->jsondat.startvolume = ele_mapvolume->number_;
        addonPtr->flags |= ADDONFLAG_STARTMAP;
        return 0;
    }
    else
    {
        LOG_F(ERROR, "Invalid startmap structure for addon %s!", addonPtr->internalId);
        LOG_F(INFO, "Valid keys are: {\"%s\", \"%s\", \"%s\"}", jsonkey_maplevel, jsonkey_mapvolume, jsonkey_mapfile);
        return -1;
    }
}

// required rendermode
static int32_t Addon_ParseJson_Rendmode(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->jsondat.rendmode = ADDON_RENDNONE;

    if (ele == nullptr) return 1;
    else if (Addon_CheckJsonStringType(addonPtr, ele, key)) return -1;

    const char* rmodestr = ele->string_;
    if (!Bstrncasecmp(rmodestr, jsonval_rendmode_classic, ARRAY_SIZE(jsonval_rendmode_classic)))
        addonPtr->jsondat.rendmode = ADDON_RENDCLASSIC;
    else if (!Bstrncasecmp(rmodestr, jsonval_rendmode_opengl, ARRAY_SIZE(jsonval_rendmode_opengl)))
        addonPtr->jsondat.rendmode = ADDON_RENDPOLYMOST;
    else if (!Bstrncasecmp(rmodestr, jsonval_rendmode_polymost, ARRAY_SIZE(jsonval_rendmode_polymost)))
        addonPtr->jsondat.rendmode = ADDON_RENDPOLYMOST;
    else if (!Bstrncasecmp(rmodestr, jsonval_rendmode_polymer, ARRAY_SIZE(jsonval_rendmode_polymer)))
        addonPtr->jsondat.rendmode = ADDON_RENDPOLYMER;
    else
    {
        LOG_F(ERROR, "Unknown rendmode '%s' in addon '%s'!", rmodestr, addonPtr->internalId);
        return -1;
    }

    return 0;
}

// Load data from json file into addon -- assumes that unique ID for the addon has been defined!
static int32_t Addon_ParseJson(useraddon_t* addonPtr, sjson_context* ctx, const char* basepath, const char* packfn)
{
    char json_path[BMAX_PATH];
    Bsnprintf(json_path, BMAX_PATH, "%s/%s", basepath, addonjsonfn);

    // open json descriptor (try 8.3 format as well, due to ken grp restrictions)
    const bool isgroup = addonPtr->loadtype & (LT_ZIP | LT_GRP | LT_SSI);
    buildvfs_kfd jsonfil = kopen4load(json_path, (isgroup ? 2 : 0));
    if (jsonfil == buildvfs_kfd_invalid)
    {
        json_path[strlen(json_path) - 1] = '\0';
        jsonfil = kopen4load(json_path, (isgroup ? 2 : 0));
        if (jsonfil == buildvfs_kfd_invalid)
            return 1; //not found, is not an addon
    }

    // load data out of file
    int32_t len = kfilelength(jsonfil);
    char* jsonTextBuf = (char *)Xmalloc(len+1);
    jsonTextBuf[len] = '\0';
    if (kread_and_test(jsonfil, jsonTextBuf, len))
    {
        LOG_F(ERROR, "Failed to access and read addon descriptor at: '%s'", json_path);
        Xfree(jsonTextBuf);
        kclose(jsonfil);
        return -1;
    }
    kclose(jsonfil);

    // parse the file contents
    sjson_reset_context(ctx);
    if (!sjson_validate(ctx, jsonTextBuf))
    {
        LOG_F(ERROR, "Syntax errors detected in addon descriptor file '%s'!", json_path);
        return -1;
    }

    int32_t parseResult, jsonErrorCnt = 0;
    sjson_node * root = sjson_decode(ctx, jsonTextBuf);
    Xfree(jsonTextBuf);

    // game type is required to identify for which game the addon should be shown in the menu
    addonPtr->gametype = Addon_ParseJson_GameFlag(addonPtr, root, jsonkey_game);
    if (addonPtr->gametype == BASEGAME_NONE)
    {
        LOG_F(ERROR, "No valid game type specified for addon: '%s'! (key: %s)", addonPtr->internalId, jsonkey_game);
        jsonErrorCnt++;
    }

    // creator must specify an identity for the addon, such that other addons can reference it
    if (Addon_ParseJson_Identity(addonPtr, root, jsonkey_depid) != 0)
    {
        LOG_F(ERROR, "Missing identity for addon: '%s'! (key: %s)", addonPtr->internalId, jsonkey_depid);
        jsonErrorCnt++;
    }

    // game crc (crc match for root parent grp)
    parseResult = Addon_ParseJson_GameCRC(addonPtr, root, jsonkey_gamecrc);
    if (parseResult == -1) jsonErrorCnt++;

    // version string (can be omitted)
    parseResult = Addon_ParseJson_Version(addonPtr, root, jsonkey_version);
    if (parseResult == -1) jsonErrorCnt++;

    // if the title isn't specified, use the package filename. On error, bail out.
    parseResult = Addon_ParseJson_String(addonPtr, root, jsonkey_title, addonPtr->jsondat.title, ADDON_MAXTITLE);
    if (parseResult == 1)
        Bstrncpy(addonPtr->jsondat.title, packfn, ADDON_MAXTITLE);
    else if (parseResult == -1)
        jsonErrorCnt++;

    // if the author isn't specified, show missing author string. On error, bail out.
    parseResult = Addon_ParseJson_String(addonPtr, root, jsonkey_author, addonPtr->jsondat.author, ADDON_MAXAUTHOR);
    if (parseResult == 1)
        Bstrncpy(addonPtr->jsondat.author, missing_author, ADDON_MAXAUTHOR);
    else if (parseResult == -1)
        jsonErrorCnt++;

    // if description missing, copy default description.
    parseResult = Addon_ParseJson_Description(addonPtr, root, jsonkey_desc);
    if (parseResult == 1)
    {
        const int desclen = strlen(missing_description) + 1;
        addonPtr->jsondat.description = (char *) Xmalloc(desclen);
        Bstrncpyz(addonPtr->jsondat.description, missing_description, desclen);
    }
    else if (parseResult == -1)
        jsonErrorCnt++;

    // rendmode (can be omitted)
    parseResult = Addon_ParseJson_Rendmode(addonPtr, root, jsonkey_rendmode);
    if (parseResult == -1) jsonErrorCnt++;

    // CON script paths (optional)
    parseResult = Addon_ParseJson_Scripts(addonPtr, root, jsonkey_con, basepath,
                    addonPtr->jsondat.main_script_path, addonPtr->jsondat.script_modules, addonPtr->jsondat.num_script_modules);
    if (parseResult == -1) jsonErrorCnt++;

    // DEF script paths
    parseResult = Addon_ParseJson_Scripts(addonPtr, root, jsonkey_def, basepath, addonPtr->jsondat.main_def_path,
                            addonPtr->jsondat.def_modules, addonPtr->jsondat.num_def_modules);
    if (parseResult == -1) jsonErrorCnt++;

    // Preview image filepath
    parseResult = Addon_ParseJson_FilePath(addonPtr, root, jsonkey_image, addonPtr->jsondat.preview_path, basepath);
    if (parseResult == -1) jsonErrorCnt++;

    // RTS file path
    parseResult = Addon_ParseJson_FilePath(addonPtr, root, jsonkey_rts, addonPtr->jsondat.main_rts_path, basepath);
    if (parseResult == -1) jsonErrorCnt++;

    // map to launch after reboot (can be omitted)
    parseResult = Addon_ParseJson_StartMap(addonPtr, root, jsonkey_startmap);
    if (parseResult == -1) jsonErrorCnt++;

    // dependencies
    parseResult = Addon_ParseJson_Dependency(addonPtr, root, jsonkey_dependencies, addonPtr->jsondat.dependencies, addonPtr->jsondat.num_dependencies);
    if (parseResult == -1) jsonErrorCnt++;

    // incompatibles
    parseResult = Addon_ParseJson_Dependency(addonPtr, root, jsonkey_incompatibles,
                                                    addonPtr->jsondat.incompatibles, addonPtr->jsondat.num_incompatibles);
    if (parseResult == -1) jsonErrorCnt++;

    if (jsonErrorCnt > 0)
    {
        LOG_F(ERROR, "Found %d errors in addon descriptor of: '%s'", jsonErrorCnt, addonPtr->internalId);
        return -1;
    }

    return 0;
}

// GRP Info functions follow

// set external identity of the grpinfo addon
static void Addon_GrpInfo_SetExternalIdentity(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    const char* identity = nullptr;
    switch (agrpf->type->crcval)
    {
#ifndef EDUKE32_STANDALONE
        case DUKEDC13_CRC:
        case DUKEDCPP_CRC:
        case DUKEDC_CRC:
        case DUKEDC_REPACK_CRC:
            identity = dukedc_id;
            break;
        case VACA13_CRC:
        case VACAPP_CRC:
        case VACA15_CRC:
        case DUKECB_CRC:
        case VACA_REPACK_CRC:
            identity = dukevaca_id;
            break;
        case DUKENW_CRC:
        case DUKENW_DEMO_CRC:
            identity = dukenw_id;
            break;
        case DZ2_13_CRC:
        case DZ2_PP_CRC:
        case DZ2_PP_REPACK_CRC:
            identity = dukezone_id;
            break;
        case PENTP_CRC:
        case PENTP_ZOOM_CRC:
            identity = dukepentp_id;
            break;
#endif
        default:
            break;
    }

    if (identity == nullptr)
        Bsnprintf(addonPtr->jsondat.externalId, ADDON_MAXID, "grpinfo_%x_%d", agrpf->type->crcval, agrpf->type->size);
    else
        Bstrncpyz(addonPtr->jsondat.externalId, identity, ADDON_MAXID);
}

// set author of the grpinfo addon
static void Addon_GrpInfo_SetAuthor(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    const char* author;
    switch (agrpf->type->crcval)
    {
#ifndef EDUKE32_STANDALONE
        case DUKEDC13_CRC:
        case DUKEDCPP_CRC:
        case DUKEDC_CRC:
        case DUKEDC_REPACK_CRC:
        case VACA13_CRC:
        case VACAPP_CRC:
        case VACA15_CRC:
        case DUKECB_CRC:
        case VACA_REPACK_CRC:
            author = author_sunstorm;
            break;
        case DUKENW_CRC:
        case DUKENW_DEMO_CRC:
        case DZ2_13_CRC:
        case DZ2_PP_CRC:
        case DZ2_PP_REPACK_CRC:
            author = author_sillysoft;
            break;
        case PENTP_CRC:
        case PENTP_ZOOM_CRC:
            author = author_intersphere;
            break;
#endif
        default:
            // TODO: grpinfo does not have an author field
            author = missing_author;
            break;
    }
    Bstrncpy(addonPtr->jsondat.author, author, ADDON_MAXAUTHOR);
}

// set description
static void Addon_GrpInfo_SetDescription(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    const char* desc;
    switch (agrpf->type->crcval)
    {
#ifndef EDUKE32_STANDALONE
        case DUKEDC13_CRC:
        case DUKEDCPP_CRC:
        case DUKEDC_CRC:
        case DUKEDC_REPACK_CRC:
            desc = dukedc_description;
            break;
        case VACA13_CRC:
        case VACAPP_CRC:
        case VACA15_CRC:
        case DUKECB_CRC:
        case VACA_REPACK_CRC:
            desc = dukevaca_description;
            break;
        case DUKENW_CRC:
        case DUKENW_DEMO_CRC:
            desc = dukenw_description;
            break;
        case DZ2_13_CRC:
        case DZ2_PP_CRC:
        case DZ2_PP_REPACK_CRC:
            desc = dukezone_description;
            break;
        case PENTP_CRC:
        case PENTP_ZOOM_CRC:
            desc = dukepentp_description;
            break;
#endif
        default:
            desc = grpinfo_description;
            break;
    }

    const int desclen = strlen(desc) + 1;
    addonPtr->jsondat.description = (char *) Xmalloc(desclen);
    Bstrncpyz(addonPtr->jsondat.description, desc, desclen);
}

// populate the contents of the addon json struct for grpinfo addons
static void Addon_GrpInfo_FakeJson(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    Addon_GrpInfo_SetExternalIdentity(addonPtr, agrpf);

    // hack: version is set to hex crc
    Bsnprintf(addonPtr->jsondat.version, ADDON_MAXVERSION, "0-%x", agrpf->type->crcval);
    Bstrncpy(addonPtr->jsondat.title, agrpf->type->name, ADDON_MAXTITLE);
    Addon_GrpInfo_SetAuthor(addonPtr, agrpf);
    Addon_GrpInfo_SetDescription(addonPtr, agrpf);
}

// Search for addons in the currently detected grpfiles
static void Addon_ReadGrpInfo(void)
{
    for (const grpfile_t *grp = foundgrps; grp; grp=grp->next)
    {
        if (grp->type->game & GAMEFLAG_ADDON)
        {
            s_useraddons[s_numuseraddons] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));
            useraddon_t* addonPtr = s_useraddons[s_numuseraddons];
            addonPtr->loadorder_idx = DEFAULT_LOADORDER_IDX;

            addonPtr->internalId = (char*) Xmalloc(ADDON_MAXID);
            Bsnprintf(addonPtr->internalId, ADDON_MAXID, "grpinfo_%x_%d", grp->type->crcval, grp->type->size);
            addonPtr->grpfile = grp;

            addonPtr->loadtype = LT_GRPINFO;
            addonPtr->gametype = (addongame_t) grp->type->game;
            addonPtr->gamecrc = grp->type->dependency;

            Addon_GrpInfo_FakeJson(addonPtr, grp);
            addonPtr->setSelected(g_selectedGrp == grp);

            s_numuseraddons++;
        }
    }
}

// close recently opened package (removes most recently opened one)
static void Addon_PackageCleanup(const int32_t grpfileidx)
{
    if (grpfileidx < numgroupfiles)
        popgroupfile(); // remove grp/ssi
    else
        popgroupfromkzstack(); // remove zip
}

// read addon packages (zip, grp, pk3...) from local folder
static void Addon_ReadLocalPackages(sjson_context* ctx, fnlist_t* fnlist, const char* addondir)
{
    for (auto & ext : addon_extensions)
    {
        BUILDVFS_FIND_REC *rec;
        fnlist_getnames(fnlist, addondir, ext, -1, 0);
        for (rec=fnlist->findfiles; rec; rec=rec->next)
        {
            char package_path[BMAX_PATH];
            int const nchar = Bsnprintf(package_path, BMAX_PATH, "%s/%s", addondir, rec->name);

            s_useraddons[s_numuseraddons] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));
            useraddon_t* addonPtr = s_useraddons[s_numuseraddons];
            addonPtr->internalId = Addon_CreateInternalIdentity(package_path, nchar);

            Bstrncpy(addonPtr->data_path, package_path, BMAX_PATH);
            addonPtr->loadorder_idx = DEFAULT_LOADORDER_IDX;

            // set initial file type based on extension
            if (!Bstrcmp(ext, grp_ext))
                addonPtr->loadtype = LT_GRP;
            else if (!Bstrcmp(ext, ssi_ext))
                addonPtr->loadtype = LT_SSI;
            else
                addonPtr->loadtype = LT_ZIP;

            // load package contents to access the json and preview within
            const int32_t grpfileidx = initgroupfile(package_path);
            if (grpfileidx == -1)
            {
                DLOG_F(ERROR, "Failed to open addon package at '%s'", package_path);
                Addon_FreeAddonContents(addonPtr);
                DO_FREE_AND_NULL(s_useraddons[s_numuseraddons]);
                continue;
            }
            else if (grpfileidx >= numgroupfiles) // zip file renamed to grp
                addonPtr->loadtype = LT_ZIP;

            // load json contents
            if (Addon_ParseJson(addonPtr, ctx, "/", rec->name))
            {
                Addon_FreeAddonContents(addonPtr);
                DO_FREE_AND_NULL(s_useraddons[s_numuseraddons]);
                Addon_PackageCleanup(grpfileidx);
                continue;
            }

            addonPtr->setSelected(CONFIG_GetAddonActivationStatus(addonPtr->internalId));
            Addon_PackageCleanup(grpfileidx);
            ++s_numuseraddons;
        }

        fnlist_clearnames(fnlist);
    }
}

// find addons from subfolders
static void Addon_ReadLocalSubfolders(sjson_context* ctx, fnlist_t* fnlist, const char* addondir)
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
        int const nchar = Bsnprintf(basepath, BMAX_PATH, "%s/%s", addondir, rec->name);

        s_useraddons[s_numuseraddons] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));
        useraddon_t* addonPtr = s_useraddons[s_numuseraddons];
        // path string is guaranteed unique
        addonPtr->internalId = Addon_CreateInternalIdentity(basepath, nchar);

        Bstrncpy(addonPtr->data_path, basepath, BMAX_PATH);
        addonPtr->loadorder_idx = DEFAULT_LOADORDER_IDX;
        addonPtr->loadtype = LT_FOLDER;

        // parse json contents
        if (Addon_ParseJson(addonPtr, ctx, basepath, rec->name))
        {
            Addon_FreeAddonContents(addonPtr);
            DO_FREE_AND_NULL(s_useraddons[s_numuseraddons]);
            continue;
        }

        addonPtr->setSelected(CONFIG_GetAddonActivationStatus(addonPtr->internalId));
        ++s_numuseraddons;
    }
    fnlist_clearnames(fnlist);
}

// find addon from Steam Workshop folders
static void Addon_ReadWorkshopItems(sjson_context* ctx)
{
    // TODO
    UNREFERENCED_PARAMETER(ctx);
}

// count potential maximum number of addons
static int32_t Addon_CountPotentialAddons(void)
{
    int32_t numaddons = 0;

    // count grpinfo addons
    for (grpfile_t *grp = foundgrps; grp; grp=grp->next)
        if (grp->type->game & GAMEFLAG_ADDON)
            numaddons++;

    char addonpathbuf[BMAX_PATH];
    if (!Addon_GetLocalDir(addonpathbuf, BMAX_PATH))
    {
        fnlist_t fnlist = FNLIST_INITIALIZER;
        fnlist_clearnames(&fnlist);

        // get number of packages in the local addon dir
        for (auto & ext : addon_extensions)
        {
            fnlist_getnames(&fnlist, addonpathbuf, ext, -1, 0);
            numaddons += fnlist.numfiles;
            fnlist_clearnames(&fnlist);
        }

        // get number of subfolders
        fnlist_getnames(&fnlist, addonpathbuf, "*", 0, -1);
        for (BUILDVFS_FIND_REC *rec = fnlist.finddirs; rec; rec=rec->next)
        {
            if (!strcmp(rec->name, ".")) continue;
            if (!strcmp(rec->name, "..")) continue;
            numaddons++;
        }
        fnlist_clearnames(&fnlist);
    }

    // TODO: get number of Steam Workshop addon folders

    return numaddons;
}

// This splits the internal addon array into the distinct types
static void Addon_SplitAddonTypes(void)
{
    for (int i = 0; i < s_numuseraddons; i++)
    {
        useraddon_t* addonPtr = s_useraddons[i];
        if (!addonPtr->isValid()) continue;
        else if (addonPtr->isGrpInfoAddon()) g_addoncount_grpinfo++;
        else if (addonPtr->isTotalConversion()) g_addoncount_tcs++;
        else g_addoncount_mods++;
    }

    g_useraddons_grpinfo = (useraddon_t **) Xcalloc(g_addoncount_grpinfo, sizeof(useraddon_t*));
    g_useraddons_tcs = (useraddon_t **) Xcalloc(g_addoncount_tcs, sizeof(useraddon_t*));
    g_useraddons_mods = (useraddon_t **) Xcalloc(g_addoncount_mods, sizeof(useraddon_t*));

    //copy data over
    int grpidx = 0, tcidx = 0, modidx = 0;
    for (int i = 0; i < s_numuseraddons; i++)
    {
        useraddon_t* addonPtr = s_useraddons[i];
        if (!addonPtr->isValid())
        {
            DO_FREE_AND_NULL(s_useraddons[i]);
            continue;
        }
        else if (addonPtr->isGrpInfoAddon()) g_useraddons_grpinfo[grpidx++] = addonPtr;
        else if (addonPtr->isTotalConversion()) g_useraddons_tcs[tcidx++] = addonPtr;
        else g_useraddons_mods[modidx++] = addonPtr;
    }
}

// Load preview image contents from an image file and convert it to palette
static int32_t Addon_LoadPreviewDataFromFile(const char *fn, uint8_t *imagebuffer)
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

    // convert to palette, this is the expensive operation...
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

// check if addon matches current game and crc, if specified
static bool Addon_MatchesGame(const useraddon_t* addonPtr)
{
    if ((addonPtr->gametype & g_gameType) == 0)
        return false;

    if (addonPtr->gamecrc == 0)
        return true;
    else
    {
        // check if selected grp, or any of the parent GRPs, match the gamecrc
        const grpfile_t * parentGrp = g_selectedGrp;
        while (parentGrp)
        {
            if (addonPtr->gamecrc == parentGrp->type->crcval)
                return true;

            if (parentGrp->type->dependency && parentGrp->type->dependency != parentGrp->type->crcval)
                parentGrp = FindGroup(parentGrp->type->dependency);
            else
                parentGrp = NULL;
        }
        return false;
    }
}

// load preview images from filesystem, convert to palette and cache them for later use
static void Addon_CachePreviewImagesForStruct(useraddon_t** useraddons, int32_t const numuseraddons)
{
    if (!useraddons || numuseraddons <= 0 || (G_GetLogoFlags() & LOGO_NOADDONS))
        return;

    // use absolute paths to load addons
    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    for (int i = 0; i < numuseraddons; i++)
    {
        useraddon_t* addonPtr = useraddons[i];

        // don't cache images for addons we won't see
        if (!addonPtr->isValid() || !Addon_MatchesGame(addonPtr) || !addonPtr->jsondat.preview_path[0])
            continue;

        intptr_t cachedImage = hash_find(&h_addonpreviews, addonPtr->jsondat.preview_path);
        if (cachedImage == -1)
        {
            addonPtr->image_data = (uint8_t *) Xmalloc(PREVIEWTILE_XSIZE * PREVIEWTILE_YSIZE * sizeof(uint8_t));
            if (addonPtr->loadtype & (LT_GRP | LT_ZIP | LT_SSI))
                initgroupfile(addonPtr->data_path);

            int const loadSuccess = Addon_LoadPreviewDataFromFile(addonPtr->jsondat.preview_path, addonPtr->image_data);

            if (addonPtr->loadtype & (LT_GRP | LT_ZIP | LT_SSI))
                Addon_PackageCleanup((addonPtr->loadtype & LT_ZIP) ? numgroupfiles : 0);

            if (loadSuccess < 0)
            {
                DO_FREE_AND_NULL(addonPtr->image_data);
                continue;
            }

            hash_add(&h_addonpreviews, addonPtr->jsondat.preview_path, (intptr_t) addonPtr->image_data, 0);
        }
        else
            addonPtr->image_data = (uint8_t*) cachedImage;
    }

    pathsearchmode = bakpathsearchmode;
}

// Initialize the load order for mods
static int16_t Addon_InitLoadOrderFromConfig(void)
{
    if (g_addoncount_mods <= 0 || !g_useraddons_mods)
        return -1;

    int16_t maxLoadOrder = 0;
    for (int i = 0; i < g_addoncount_mods; i++)
    {
        useraddon_t* addonPtr = g_useraddons_mods[i];

        // sanity checks in case something goes wrong
        if (!addonPtr->isValid() || !Addon_MatchesGame(addonPtr)
                || addonPtr->isTotalConversion() || addonPtr->isGrpInfoAddon())
        {
            DLOG_F(WARNING, "Skip invalid addon in load order init: %s", addonPtr->internalId);
            continue;
        }

        addonPtr->loadorder_idx = CONFIG_GetAddonLoadOrder(addonPtr->internalId);
        if (addonPtr->loadorder_idx < 0)
            addonPtr->loadorder_idx = 0;
        else if (addonPtr->loadorder_idx > maxLoadOrder)
            maxLoadOrder = addonPtr->loadorder_idx;
    }

    return maxLoadOrder + 1;
}

// save the mod load order
static void Addon_SaveModsConfig(void)
{
    for (int i = 0; i < g_addoncount_mods; i++)
    {
        const useraddon_t* addonPtr = g_useraddons_mods[i];
        // sanity checks in case something goes wrong
        if (!addonPtr->isValid() || !Addon_MatchesGame(addonPtr)
                || addonPtr->isTotalConversion() || addonPtr->isGrpInfoAddon())
        {
            DLOG_F(WARNING, "Skip invalid addon in load order init: %s. This shouldn't be happening.", addonPtr->internalId);
            continue;
        }

        CONFIG_SetAddonActivationStatus(addonPtr->internalId, addonPtr->isSelected());
        CONFIG_SetAddonLoadOrder(addonPtr->internalId, addonPtr->loadorder_idx);
    }
}

// Prepare the content from the given addon for loading
static int32_t Addon_PrepareUserAddon(const useraddon_t* addonPtr)
{
    if (!addonPtr->data_path[0])
    {
        LOG_F(ERROR, "No data path specified for addon: %s", addonPtr->internalId);
        return -1;
    }

    switch (addonPtr->loadtype)
    {
        case LT_FOLDER:
        case LT_WORKSHOP:
            if (addsearchpath_user(addonPtr->data_path, SEARCHPATH_REBOOT))
            {
                LOG_F(ERROR, "Failed to add search path '%s' of addon: %s", addonPtr->data_path, addonPtr->internalId);
                return -1;
            }
            break;
        case LT_ZIP:
        case LT_SSI:
        case LT_GRP:
            if ((initgroupfile(addonPtr->data_path)) == -1)
            {
                LOG_F(ERROR, "Failed to open group file '%s' of addon: %s", addonPtr->data_path, addonPtr->internalId);
                return -1;
            }
            break;
        case LT_GRPINFO:
        case LT_INVALID:
            LOG_F(ERROR, "Invalid addon: %s", addonPtr->internalId);
            return -1;
    }

    if (addonPtr->jsondat.main_script_path[0])
        G_AddCon(addonPtr->jsondat.main_script_path);

    for (int i = 0; i < addonPtr->jsondat.num_script_modules; i++)
        G_AddConModule(addonPtr->jsondat.script_modules[i]);

    if (addonPtr->jsondat.main_def_path[0])
        G_AddDef(addonPtr->jsondat.main_def_path);

    for (int i = 0; i < addonPtr->jsondat.num_def_modules; i++)
        G_AddDefModule(addonPtr->jsondat.def_modules[i]);

    if (addonPtr->jsondat.main_rts_path[0])
    {
        Bstrncpy(ud.rtsname, addonPtr->jsondat.main_rts_path, MAXRTSNAME);
        LOG_F(INFO, "Using RTS file: %s", ud.rtsname);
    }

    return 0;
}

// check whether the addon satisifies the dependency
static int32_t Addon_DependencyMatch(const addondependency_t* depPtr, const useraddon_t* otherAddonPtr)
{
    // if uid doesn't match, not fulfilled
    if (Bstrcmp(depPtr->depId, otherAddonPtr->jsondat.externalId))
        return false;

    const char* packVersion = otherAddonPtr->jsondat.version;
    const char* depVersion = depPtr->version;

    // if package or dependency has no version specified, match true
    if (!packVersion[0] || !depVersion[0] || depPtr->cOp == VCOMP_NOOP)
        return true;

    int const result = Addon_CompareVersionStrings(packVersion, depVersion);
    switch (depPtr->cOp)
    {
        case VCOMP_EQ:   return (result == 0);
        case VCOMP_GT:   return (result > 0);
        case VCOMP_GTEQ: return (result >= 0);
        case VCOMP_LT:   return (result < 0);
        case VCOMP_LTEQ: return (result <= 0);
        default:
            LOG_F(ERROR, "Unimplemented comparator %d for dependency %s. This shouldn't be happening.", depPtr->cOp, depPtr->depId);
            return false;
    }
}

// update an addon's dependency state (or incompatibles)
static void Addon_UpdateDependencies(useraddon_t* addonPtr, addondependency_t* depList, const int32_t depCount)
{
    // grp info addons have no dependencies
    if (addonPtr->isGrpInfoAddon())
        return;

    for (int i = 0; i < depCount; i++)
    {
        addondependency_t & dep = depList[i];
        dep.setFulfilled(false);

        // check selected grpinfo
        for (int j = 0; j < g_addoncount_grpinfo; j++)
        {
            if (g_useraddons_grpinfo[j]->isSelected() && Addon_DependencyMatch(&dep, g_useraddons_grpinfo[j]))
            {
                dep.setFulfilled(true);
                break;
            }
        }

        if (dep.isFulfilled())
            continue;

        if (!addonPtr->isTotalConversion())
        {
            // TCs may depend on mods and grpinfo, but not other TCs
            for (int j = 0; j < g_addoncount_tcs; j++)
            {
                if (g_useraddons_tcs[j]->isSelected() && Addon_DependencyMatch(&dep, g_useraddons_tcs[j]))
                {
                    dep.setFulfilled(true);
                    break;
                }
            }

            if (dep.isFulfilled())
                continue;
        }

        // TODO: some mods may depend on load order, need some way to express this in the JSON
        for (int j = 0; j < g_addoncount_mods; j++)
        {
            if (g_useraddons_mods[j]->isSelected() && Addon_DependencyMatch(&dep, g_useraddons_mods[j]))
            {
                dep.setFulfilled(true);
                break;
            }
        }
    }
}

// update global counter for selected addons
static void Addon_UpdateCount_SelectedAddons(void)
{
    int i; useraddon_t * addonPtr;
    g_num_selected_addons = 0;
    ITER_GRPINFO(i, addonPtr) g_num_selected_addons += addonPtr->isSelected();
    ITER_MODS(i, addonPtr) g_num_selected_addons += addonPtr->isSelected();
    ITER_TCS(i, addonPtr) g_num_selected_addons += addonPtr->isSelected();
}

// update global counter for active missing dependencies
static void increment_global_mdepscounter(const char*, intptr_t) { g_num_active_mdeps++; } //hack
static void Addon_UpdateCount_MissingDependencies(void)
{
    int i; useraddon_t * addonPtr;
    hash_init(&h_addontemp);

    g_num_active_mdeps = 0;
    ITER_GRPINFO(i, addonPtr) addonPtr->countMissingDependencies((addonPtr->isSelected()) ?  &h_addontemp : nullptr);
    ITER_TCS(i, addonPtr) addonPtr->countMissingDependencies((addonPtr->isSelected()) ?  &h_addontemp : nullptr);
    ITER_MODS(i, addonPtr) addonPtr->countMissingDependencies((addonPtr->isSelected()) ?  &h_addontemp : nullptr);

    hash_loop(&h_addontemp, increment_global_mdepscounter);
    hash_free(&h_addontemp);
}

// update global counter for active incompatibilities
static void increment_global_incompatiblescounter(const char*, intptr_t) { g_num_active_incompats++; } //hack
static void Addon_UpdateCount_ActiveIncompatibles(void)
{
    int i; useraddon_t * addonPtr;
    hash_init(&h_addontemp);

    g_num_active_incompats = 0;
    ITER_GRPINFO(i, addonPtr) addonPtr->countIncompatibleAddons((addonPtr->isSelected()) ?  &h_addontemp : nullptr);
    ITER_TCS(i, addonPtr) addonPtr->countIncompatibleAddons((addonPtr->isSelected()) ?  &h_addontemp : nullptr);
    ITER_MODS(i, addonPtr) addonPtr->countIncompatibleAddons((addonPtr->isSelected()) ?  &h_addontemp : nullptr);

    hash_loop(&h_addontemp, increment_global_incompatiblescounter);
    hash_free(&h_addontemp);
}

static void Addon_UpdateSelectedRendmode(void)
{
    int i; useraddon_t* addonPtr;
    g_addon_selrendmode = ADDON_RENDNONE;
    ITER_GRPINFO(i, addonPtr) if (addonPtr->isSelected()) g_addon_selrendmode |= addonPtr->jsondat.rendmode;
    ITER_MODS(i, addonPtr) if (addonPtr->isSelected()) g_addon_selrendmode |= addonPtr->jsondat.rendmode;
    ITER_TCS(i, addonPtr) if (addonPtr->isSelected()) g_addon_selrendmode |= addonPtr->jsondat.rendmode;
}

// utility to free preview image storage
static void freehashpreviewimage(const char *, intptr_t key)
{
    Xfree((void *)key);
}

void Addon_FreePreviewHashTable(void)
{
    hash_loop(&h_addonpreviews, freehashpreviewimage);
    hash_free(&h_addonpreviews);
}

void Addon_FreeUserAddons(void)
{
    Addon_FreeUserAddonsForStruct(g_useraddons_grpinfo, g_addoncount_grpinfo);
    Addon_FreeUserAddonsForStruct(g_useraddons_tcs, g_addoncount_tcs);
    Addon_FreeUserAddonsForStruct(g_useraddons_mods, g_addoncount_mods);
}

// Important: this function is called before the setup window is shown
// Hence it must not depend on any variables initialized from game content
void Addon_LoadDescriptors(void)
{
    // free previous storage
    Addon_FreeUserAddons();

    // initialize hash table if it doesn't exist yet
    if (!h_addonpreviews.items)
        hash_init(&h_addonpreviews);

    // use absolute paths to load addons
    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    // create space for all potentially valid addons
    int32_t maxaddons = Addon_CountPotentialAddons();
    if (maxaddons <= 0)
        return;

    // these variables are updated over the following functions
    s_useraddons = (useraddon_t **) Xcalloc(maxaddons, sizeof(useraddon_t*));
    s_numuseraddons = 0;

    Addon_ReadGrpInfo();

    sjson_context * ctx = sjson_create_context(0, 0, nullptr);
    char addonpathbuf[BMAX_PATH];
    if (!Addon_GetLocalDir(addonpathbuf, BMAX_PATH))
    {
        fnlist_t fnlist = FNLIST_INITIALIZER;
        fnlist_clearnames(&fnlist);
        Addon_ReadLocalPackages(ctx, &fnlist, addonpathbuf);
        Addon_ReadLocalSubfolders(ctx, &fnlist, addonpathbuf);
    }

    Addon_ReadWorkshopItems(ctx);
    sjson_destroy_context(ctx);

    pathsearchmode = bakpathsearchmode;

    if (s_numuseraddons > 0)
        Addon_SplitAddonTypes();

    DO_FREE_AND_NULL(s_useraddons);
    s_numuseraddons = 0;
}

// necessary evil because root GRP and gametype are not known before setup window is shown
// removes all addons that are not available for the currently selected game
void Addon_PruneInvalidAddons(useraddon_t** & useraddons, int32_t & numuseraddons)
{
    if (!useraddons || numuseraddons <= 0)
        return;

    int i, j, newaddoncount = 0;
    for (i = 0; i < numuseraddons; i++)
    {
        useraddon_t* addonPtr = useraddons[i];
        if (addonPtr->isValid() && Addon_MatchesGame(addonPtr))
            newaddoncount++;
    }

    useraddon_t** gooduseraddons = (useraddon_t **) Xcalloc(newaddoncount, sizeof(useraddon_t*));

    for (i=0, j=0; i < numuseraddons; i++)
    {
        useraddon_t* addonPtr = useraddons[i];
        if (addonPtr->isValid()  && Addon_MatchesGame(addonPtr))
            gooduseraddons[j++] = addonPtr;
        else
        {
            Addon_FreeAddonContents(addonPtr);
            Xfree(addonPtr);
        }
    }
    Xfree(useraddons);

    useraddons = gooduseraddons;
    numuseraddons = newaddoncount;
}

// initializing of preview images requires access to palette, and is run after game content is loaded
// hence this may depend on variables such as g_gameType or Logo Flags
void Addon_CachePreviewImages(void)
{
    Addon_CachePreviewImagesForStruct(g_useraddons_grpinfo, g_addoncount_grpinfo);
    Addon_CachePreviewImagesForStruct(g_useraddons_tcs, g_addoncount_tcs);
    Addon_CachePreviewImagesForStruct(g_useraddons_mods, g_addoncount_mods);
}

// Load data from cache into the tilespace
int32_t Addon_LoadPreviewTile(const useraddon_t * addonPtr)
{
    if (!addonPtr->image_data)
        return -1;

    walock[TILE_ADDONSHOT] = CACHE1D_PERMANENT;

    if (waloff[TILE_ADDONSHOT] == 0)
        g_cache.allocateBlock(&waloff[TILE_ADDONSHOT], PREVIEWTILE_XSIZE * PREVIEWTILE_YSIZE, &walock[TILE_ADDONSHOT]);

    tilesiz[TILE_ADDONSHOT].x = PREVIEWTILE_XSIZE;
    tilesiz[TILE_ADDONSHOT].y = PREVIEWTILE_YSIZE;

    Bmemcpy((char *)waloff[TILE_ADDONSHOT], addonPtr->image_data, PREVIEWTILE_XSIZE * PREVIEWTILE_YSIZE);
    tileInvalidate(TILE_ADDONSHOT, 0, 255);
    return 0;
}

// initialize load order for mods, and sanitize it so there are no gaps or duplicates
void Addon_InitializeLoadOrder(void)
{
    if (g_addoncount_mods <= 0 || !g_useraddons_mods)
        return;

    int32_t i, cl, maxBufSize;
    int16_t maxLoadOrder = Addon_InitLoadOrderFromConfig();

    // allocate enough space for the case where all load order indices are duplicates
    maxBufSize = maxLoadOrder + g_addoncount_mods;
    useraddon_t** lobuf = (useraddon_t**) Xcalloc(maxBufSize, sizeof(useraddon_t*));

    // place pointers to menu addons corresponding to load order
    for (i = 0; i < g_addoncount_mods; i++)
    {
        useraddon_t* addonPtr = g_useraddons_mods[i];
        if (addonPtr->isTotalConversion() || addonPtr->isGrpInfoAddon())
            continue;

        cl = addonPtr->loadorder_idx;

        if (cl < 0 || lobuf[cl])
            lobuf[maxLoadOrder++] = addonPtr;
        else
            lobuf[cl] = addonPtr;
    }

    // clean up load order
    int16_t newlo = 0;
    for (i = 0; i < maxLoadOrder; i++)
    {
        if (lobuf[i])
        {
            lobuf[i]->loadorder_idx = newlo;
            newlo++;
        }
    }
    Xfree(lobuf);
    Addon_SaveModsConfig();
}

// swap the load order between two addons
void Addon_SwapLoadOrder(int32_t const indexA, int32_t const indexB, int32_t const maxvis)
{
    useraddon_t* addonA = g_useraddons_mods[indexA];
    useraddon_t* addonB = g_useraddons_mods[indexB];

    int temp = addonA->loadorder_idx;
    addonA->loadorder_idx = addonB->loadorder_idx;
    addonB->loadorder_idx = temp;

    addonA->updateMenuEntryName(0, maxvis);
    addonB->updateMenuEntryName(0, maxvis);

    Addon_SaveModsConfig();
}

// update dependency states of all addons, based on currently selected addons
void Addon_RefreshDependencyStates(void)
{
    int i; useraddon_t * addonPtr;
    ITER_TCS(i, addonPtr)
    {
        Addon_UpdateDependencies(addonPtr, addonPtr->jsondat.dependencies, addonPtr->jsondat.num_dependencies);
        Addon_UpdateDependencies(addonPtr, addonPtr->jsondat.incompatibles, addonPtr->jsondat.num_incompatibles);
    }

    ITER_MODS(i, addonPtr)
    {
        Addon_UpdateDependencies(addonPtr, addonPtr->jsondat.dependencies, addonPtr->jsondat.num_dependencies);
        Addon_UpdateDependencies(addonPtr, addonPtr->jsondat.incompatibles, addonPtr->jsondat.num_incompatibles);
    }

    Addon_UpdateCount_SelectedAddons();
    Addon_UpdateCount_MissingDependencies();
    Addon_UpdateCount_ActiveIncompatibles();
    Addon_UpdateSelectedRendmode();

    // DLOG_F(INFO, "Number of selected addons: %d", g_num_selected_addons);
    // DLOG_F(INFO, "Number of missing dependencies of selected addons: %d", g_num_active_mdeps);
    // DLOG_F(INFO, "Number of addons that are incompatible with selected addons: %d", g_num_active_incompats);
}

bool Addon_GetStartMap(const char* & startfn, int32_t & startlevel, int32_t & startvolume)
{
    // assume that load order already sanitized, each index unique
    useraddon_t** lobuf = (useraddon_t**) Xcalloc(g_addoncount_mods, sizeof(useraddon_t*));
    for (int i = 0; i < g_addoncount_mods; i++)
        lobuf[g_useraddons_mods[i]->loadorder_idx] = g_useraddons_mods[i];

    // addons in reverse load order
    for (int i = g_addoncount_mods-1; i >= 0; i--)
    {
        useraddon_t* addonPtr = lobuf[i];
        if (!addonPtr->isValid() || !addonPtr->isSelected() || !Addon_MatchesGame(addonPtr))
            continue;

        if (addonPtr->flags & ADDONFLAG_STARTMAP)
        {
            startlevel = addonPtr->jsondat.startlevel;
            startvolume = addonPtr->jsondat.startvolume;
            startfn = addonPtr->jsondat.boardfilename;
            return true;
        }
    }

    // addons in reverse load order
    for (int i = g_addoncount_tcs-1; i >= 0; i--)
    {
        useraddon_t* addonPtr = g_useraddons_tcs[i];
        if (!addonPtr->isValid() || !addonPtr->isSelected() || !Addon_MatchesGame(addonPtr))
            continue;

        if (addonPtr->flags & ADDONFLAG_STARTMAP)
        {
            startlevel = addonPtr->jsondat.startlevel;
            startvolume = addonPtr->jsondat.startvolume;
            startfn = addonPtr->jsondat.boardfilename;
            return true;
        }
    }

    startlevel = -1;
    startvolume = -1;
    startfn = nullptr;
    return false;
}

#ifdef USE_OPENGL
int32_t Addon_GetBootRendmode(void)
{
    if (!(g_bootState & BOOTSTATE_ADDONS))
        return -1;

    switch (g_addon_selrendmode)
    {
        case ADDON_RENDCLASSIC: return REND_CLASSIC;
        case ADDON_RENDPOLYMOST: return REND_POLYMOST;
#ifdef POLYMER
        case ADDON_RENDPOLYMER: return REND_POLYMER;
#endif
        default: return -1;
    }
}
#endif

// iterate through all grp info addons, find selected one, change game grp
int32_t Addon_PrepareGrpInfoAddons(void)
{
    if (!(g_bootState & BOOTSTATE_ADDONS) || g_addoncount_grpinfo <= 0 || !g_useraddons_grpinfo)
        return 0;

    // do not load grpinfo files on first boot
    if ((g_bootState & BOOTSTATE_INITIAL))
        return 1;

    // addons in load order
    for (int i = 0; i < g_addoncount_grpinfo; i++)
    {
        const useraddon_t* addonPtr = g_useraddons_grpinfo[i];
        if (!addonPtr->isSelected() || !Addon_MatchesGame(addonPtr))
            continue;

        if (!addonPtr->isValid() || addonPtr->isTotalConversion() || !addonPtr->isGrpInfoAddon() || !addonPtr->grpfile)
        {
            LOG_F(ERROR, "Skip invalid grpinfo in init: %s. This shouldn't be happening.", addonPtr->internalId);
            continue;
        }

        g_selectedGrp = addonPtr->grpfile;
        break; // only load one at most
    }

    return 0;
}

// iterate through all tcs, find selected one, initialize data
int32_t Addon_PrepareUserTCs(void)
{
    if (!(g_bootState & BOOTSTATE_ADDONS) || g_addoncount_tcs <= 0 || !g_useraddons_tcs)
        return 0;

    // use absolute paths to load addons
    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    // addons in load order
    for (int i = 0; i < g_addoncount_tcs; i++)
    {
        const useraddon_t* addonPtr = g_useraddons_tcs[i];
        if (!addonPtr->isSelected() || !Addon_MatchesGame(addonPtr))
            continue;

        // sanity checks
        if (!addonPtr->isValid() || !addonPtr->isTotalConversion() || addonPtr->isGrpInfoAddon())
        {
            LOG_F(ERROR, "Skip invalid addon in TC init: %s. This shouldn't be happening.", addonPtr->internalId);
            continue;
        }

        Addon_PrepareUserAddon(addonPtr);
        break; // only load one at most
    }

    pathsearchmode = bakpathsearchmode;
    return 0;
}

// iterate through all mods in load order, find selected ones, initialize data
int32_t Addon_PrepareUserMods(void)
{
    if (!(g_bootState & BOOTSTATE_ADDONS) || g_addoncount_mods <= 0 || !g_useraddons_mods)
        return 0;

    // use absolute paths to load addons
    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    // assume that load order already sanitized, each index unique
    useraddon_t** lobuf = (useraddon_t**) Xcalloc(g_addoncount_mods, sizeof(useraddon_t*));
    for (int i = 0; i < g_addoncount_mods; i++)
        lobuf[g_useraddons_mods[i]->loadorder_idx] = g_useraddons_mods[i];

    // addons in load order
    for (int i = 0; i < g_addoncount_mods; i++)
    {
        useraddon_t* addonPtr = lobuf[i];
        if (!addonPtr->isSelected() || !Addon_MatchesGame(addonPtr))
            continue;

        // sanity checks
        if (!addonPtr->isValid() || addonPtr->isTotalConversion() || addonPtr->isGrpInfoAddon())
        {
            DLOG_F(WARNING, "Skip invalid addon in mod init: %s. This shouldn't be happening.", addonPtr->internalId);
            continue;
        }

        Addon_PrepareUserAddon(addonPtr);
    }

    pathsearchmode = bakpathsearchmode;

    Xfree(lobuf);
    return 0;
}
