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

// supported package extensions
static const char grp_ext[] = "*.grp";
static const char ssi_ext[] = "*.ssi";
static const char* addon_extensions[] = { grp_ext, ssi_ext, "*.zip", "*.pk3", "*.pk4" };

// local addon folder name and json descriptor filename
static const char addondirname[] = "addons";
static const char addonjsonfn[] = "addon.json";

// temporary storage for all addons -- all addons are first stored in this array before being separated by type
static useraddon_t** s_useraddons = nullptr;
static int32_t s_numuseraddons = 0;

// addons that replace the main CON/main DEF file are identified as TCs
useraddon_t** g_useraddons_tcs = nullptr;
int32_t g_addoncount_tcs = 0;

// all remaining addons are identified as mods
useraddon_t** g_useraddons_mods = nullptr;
int32_t g_addoncount_mods = 0;

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
static const char jsonkey_dependencies[] = "dependencies";
static const char jsonkey_incompatibles[] = "incompatibles";
static const char jsonkey_rendmodes[] = "rendmodes";
static const char jsonkey_startmap[] = "startmap";

static const char* json_basekeys[] =
{
    jsonkey_depid, jsonkey_game, jsonkey_gamecrc, jsonkey_version,jsonkey_title,
    jsonkey_author, jsonkey_desc, jsonkey_image, jsonkey_con, jsonkey_def, jsonkey_rts,
    jsonkey_dependencies,jsonkey_incompatibles,jsonkey_rendmodes,jsonkey_startmap
};

// script subkeys
static const char jsonkey_scripttype[] = "type";
static const char jsonkey_scriptpath[] = "path";
static const char* json_scriptkeys[] = {jsonkey_scripttype, jsonkey_scriptpath};

// dependency subkeys
static const char* json_dependencykeys[] = {jsonkey_depid, jsonkey_version};

// map start subkeys
static const char jsonkey_mapvolume[] = "volume";
static const char jsonkey_maplevel[] = "level";
static const char jsonkey_mapfile[] = "file";
static const char* json_startmapkeys[] = {jsonkey_mapvolume, jsonkey_maplevel, jsonkey_mapfile};

// string sequences to identify different gametypes
static const char jsonval_gt_any[] = "any";
static const char jsonval_gt_duke[] = "duke3d";
static const char jsonval_gt_nam[] = "nam";
static const char jsonval_gt_ww2gi[] = "ww2gi";
static const char jsonval_gt_fury[] = "fury";

// string sequences to identify script type
static const char jsonval_scriptmain[] = "main";
static const char jsonval_scriptmodule[] = "module";

// rendmodes
static const char jsonval_rendmode_classic[] = "classic";
static const char jsonval_rendmode_opengl[] = "opengl";
static const char jsonval_rendmode_polymost[] = "polymost";
static const char jsonval_rendmode_polymer[] = "polymer";

// utility to check for file existence
static int32_t AJ_CheckFilePresence(const char* filepath)
{
    buildvfs_kfd jsonfil = kopen4load(filepath, 0);
    if (jsonfil != buildvfs_kfd_invalid)
    {
        kclose(jsonfil);
        return 0;
    }

    return -1;
}

// This awful function verifies that version strings adhere to the following format:
// REGEX: ((([1-9][0-9]*)|0)\.)*(([1-9][0-9]*))(\-[a-zA-Z0-9]+)?
// valid examples: {1.0, 1.0.3.4-alphanum123, 2, 4.0-a}
// This would be better accomplished with a regular expression library
static int32_t AJ_CheckVersionFormat(const char* versionString)
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

// the version string in the dependency portion is prepended with comparison characters
static int32_t AJ_SetupDependencyVersion(addondependency_t * dep, const char* versionString)
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

    if (AJ_CheckVersionFormat(dep->version))
    {
        LOG_F(ERROR, "Version string '%s' has incorrect format!", dep->version);
        dep->version[0] = '\0'; dep->cOp = VCOMP_NOOP;
        return -1;
    }

    return 0;
}

static void AJ_CheckUnknownKeys(const char* json_fn, sjson_node *node, const char* parentkey,
                                        const char **keylist, int32_t const numkeys)
{
    sjson_node* child;
    sjson_foreach(child, node)
    {
        bool foundkey = false;
        for (int k = 0; k < numkeys; k++)
        {
            if (!Bstrcasecmp(child->key, keylist[k]))
            {
                foundkey = true;
                break;
            }
        }
        if (!foundkey)
        {
            if (parentkey != nullptr)
                LOG_F(WARNING, "Unknown key \"%s\" of parent \"%s\" in json of: %s", child->key, parentkey, json_fn);
            else
                LOG_F(WARNING, "Unknown root key \"%s\" in json of: %s", child->key, json_fn);
        }
    }
}

// utility to check if element is string typed
static int32_t AJ_CheckStringTyped(const useraddon_t *addonPtr, sjson_node *ele, const char *key)
{
    if (ele->tag != SJSON_STRING)
    {
        LOG_F(ERROR, "Addon descriptor member '%s' of addon '%s' is not string typed!", key, addonPtr->internalId);
        return -1;
    }
    return 0;
}

static int32_t AJ_CheckIdentityFormat(const useraddon_t *addonPtr, const char* ident)
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
static int32_t AJ_ParseIdentity(useraddon_t *addonPtr, sjson_node *root, const char *key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->jsondat.externalId[0] = '\0';

    if (ele == nullptr) return 1;
    else if (AJ_CheckStringTyped(addonPtr, ele, key) || AJ_CheckIdentityFormat(addonPtr, ele->string_))
        return -1;

    Bstrncpy(addonPtr->jsondat.externalId, ele->string_, ADDON_MAXID);
    return 0;
}

// parse arbitrary string
static int32_t AJ_ParseString(useraddon_t *addonPtr, sjson_node *root, const char *key,
                                        char *dstbuf, int32_t const bufsize)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    dstbuf[0] = '\0';

    if (ele == nullptr) return 1;
    else if (AJ_CheckStringTyped(addonPtr, ele, key)) return -1;

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
static int32_t AJ_ParseFilePath(useraddon_t* addonPtr, sjson_node* root, const char* key, char *dstbuf, const char* basepath)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    dstbuf[0] = '\0';

    if (ele == nullptr) return 1;
    else if (AJ_CheckStringTyped(addonPtr, ele, key)) return -1;

    // if exceeds maxsize, file presence check will fail anyways
    Bsnprintf(dstbuf, BMAX_PATH, "%s/%s", basepath, ele->string_);
    if (AJ_CheckFilePresence(addonPtr->jsondat.preview_path))
    {
        LOG_F(ERROR, "File for key '%s' of addon '%s' at location '%s' does not exist!", key, addonPtr->internalId, dstbuf);
        dstbuf[0] = '\0';
        return -1;
    }

    Bcorrectfilename(dstbuf, 0);
    return 0;
}

// parse version and check its format
static int32_t AJ_ParseVersion(useraddon_t *addonPtr, sjson_node *root, const char *key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->jsondat.version[0] = '\0';

    if (ele == nullptr) return 1;
    else if (AJ_CheckStringTyped(addonPtr, ele, key)) return -1;

    Bstrncpy(addonPtr->jsondat.version, ele->string_, ADDON_MAXVERSION);
    if (addonPtr->jsondat.version[ADDON_MAXVERSION-1])
    {
        LOG_F(ERROR, "Member '%s' of addon '%s' exceeds maximum size of %d chars!", key, addonPtr->internalId, ADDON_MAXVERSION);
        addonPtr->jsondat.version[0] = '\0';
        return -1;
    }

    if (AJ_CheckVersionFormat(addonPtr->jsondat.version))
    {
        LOG_F(ERROR, "Version string '%s' of addon %s has incorrect format!", addonPtr->jsondat.version, addonPtr->internalId);
        addonPtr->jsondat.version[0] = '\0';
        return -1;
    }

    return 0;
}

// retrieve the description -- in this case we allocate new memory, rather than just copying the string into an existing buffer
static int32_t AJ_ParseDescription(useraddon_t *addonPtr, sjson_node *root, const char *key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->jsondat.description = nullptr;

    if (ele == nullptr) return 1;
    else if (AJ_CheckStringTyped(addonPtr, ele, key)) return -1;

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
static int32_t AJ_ParseScriptModules(useraddon_t *addonPtr, sjson_node* root, const char* key, const char* basepath,
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

        AJ_CheckUnknownKeys(addonPtr->internalId, snode, key, json_scriptkeys, ARRAY_SIZE(json_scriptkeys));

        script_path = sjson_find_member_nocase(snode, jsonkey_scriptpath);
        if (script_path == nullptr || script_path->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Script path missing or has invalid format in addon '%s'!", addonPtr->internalId);
            hasError = true;
            continue;
        }

        Bsnprintf(scriptbuf, BMAX_PATH, "%s/%s", basepath, script_path->string_);
        if (AJ_CheckFilePresence(scriptbuf))
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

// get the dependency property
static int32_t AJ_ParseDependencyList(useraddon_t* addonPtr, sjson_node* root, const char* key,
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

        AJ_CheckUnknownKeys(addonPtr->internalId, snode, key, json_dependencykeys, ARRAY_SIZE(json_dependencykeys));

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
        if (AJ_CheckIdentityFormat(addonPtr, dep_uid->string_))
            continue;
        Bstrncpy(adt.depId, dep_uid->string_, ADDON_MAXID);

        // only bail if string specified and invalid, otherwise accept dependencies without version
        if (dep_version && AJ_SetupDependencyVersion(&adt, dep_version->string_))
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
static addongame_t AJ_ParseGameFlag(const useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    if (ele == nullptr || AJ_CheckStringTyped(addonPtr, ele, key))
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
static int32_t AJ_ParseGameCRC(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->gamecrc = 0;

    if (ele == nullptr) return 1;
    else if (AJ_CheckStringTyped(addonPtr, ele, key)) return -1;

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
static int32_t AJ_ParseStartMap(useraddon_t* addonPtr, sjson_node* root, const char* key)
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

    AJ_CheckUnknownKeys(addonPtr->internalId, ele, key, json_startmapkeys, ARRAY_SIZE(json_startmapkeys));

    sjson_node* ele_mapfile = sjson_find_member_nocase(ele, jsonkey_mapfile);
    if (ele_mapfile)
    {
        if (AJ_CheckStringTyped(addonPtr, ele_mapfile, jsonkey_mapfile))
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

static int32_t AJ_SetRendmodeFromString(useraddon_t* addonPtr, const char* rmodestr)
{
    if (!Bstrncasecmp(rmodestr, jsonval_rendmode_classic, ARRAY_SIZE(jsonval_rendmode_classic)))
        addonPtr->jsondat.compat_rendmodes |= ADDON_RENDCLASSIC;
    else if (!Bstrncasecmp(rmodestr, jsonval_rendmode_opengl, ARRAY_SIZE(jsonval_rendmode_opengl)))
        addonPtr->jsondat.compat_rendmodes |= ADDON_RENDPOLYMOST;
    else if (!Bstrncasecmp(rmodestr, jsonval_rendmode_polymost, ARRAY_SIZE(jsonval_rendmode_polymost)))
        addonPtr->jsondat.compat_rendmodes |= ADDON_RENDPOLYMOST;
    else if (!Bstrncasecmp(rmodestr, jsonval_rendmode_polymer, ARRAY_SIZE(jsonval_rendmode_polymer)))
        addonPtr->jsondat.compat_rendmodes |= ADDON_RENDPOLYMER;
    else
    {
        LOG_F(ERROR, "Unknown rendmode '%s' in addon '%s'!", rmodestr, addonPtr->internalId);
        return -1;
    }

    return 0;
}

// required rendermode
static int32_t AJ_ParseRendmode(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    addonPtr->jsondat.compat_rendmodes = ADDON_RENDNONE;
    sjson_node * ele = sjson_find_member_nocase(root, key);
    if (ele == nullptr) return 1;

    if (ele->tag == SJSON_STRING)
    {
        return AJ_SetRendmodeFromString(addonPtr, ele->string_);
    }
    else if (ele->tag == SJSON_ARRAY)
    {
        sjson_node *child;
        sjson_foreach(child, ele)
        {
            if (child->tag != SJSON_STRING || AJ_SetRendmodeFromString(addonPtr, child->string_))
            {
                LOG_F(ERROR, "Invalid type in array of key %s for addon: '%s'!", key, addonPtr->internalId);
                return -1;
            }
        }
        return 0;
    }
    else
    {
        LOG_F(ERROR, "Invalid value type for key '%s' in addon :'%s'!", key, addonPtr->internalId);
        return -1;
    }
}

// Load data from json file into addon -- assumes that unique ID for the addon has been defined!
int32_t AJ_ParseJsonDescriptor(char* json_fn, useraddon_t* addonPtr, const char* basepath, const char* packfn)
{
    // open json descriptor (try 8.3 format as well, due to ken grp restrictions)
    const bool isgroup = addonPtr->loadtype & (LT_ZIP | LT_GRP | LT_SSI);
    buildvfs_kfd jsonfil = kopen4load(json_fn, (isgroup ? 2 : 0));
    if (jsonfil == buildvfs_kfd_invalid)
    {
        json_fn[strlen(json_fn) - 1] = '\0';
        jsonfil = kopen4load(json_fn, (isgroup ? 2 : 0));
        if (jsonfil == buildvfs_kfd_invalid)
            return 1; //not found, is not an addon
    }

    // load data out of file
    int32_t len = kfilelength(jsonfil);
    char* jsonTextBuf = (char *)Xmalloc(len+1);
    jsonTextBuf[len] = '\0';
    if (kread_and_test(jsonfil, jsonTextBuf, len))
    {
        LOG_F(ERROR, "Failed to access and read addon descriptor at: '%s'", json_fn);
        Xfree(jsonTextBuf);
        kclose(jsonfil);
        return -1;
    }
    kclose(jsonfil);

    // parse the file contents
    sjson_context * ctx = sjson_create_context(0, 0, nullptr);
    if (!sjson_validate(ctx, jsonTextBuf))
    {
        LOG_F(ERROR, "Syntax errors detected in addon descriptor file '%s'!", json_fn);
        sjson_destroy_context(ctx);
        return -1;
    }

    int32_t parseResult, jsonErrorCnt = 0;
    sjson_node * root = sjson_decode(ctx, jsonTextBuf);
    Xfree(jsonTextBuf);

    AJ_CheckUnknownKeys(addonPtr->internalId, root, nullptr, json_basekeys, ARRAY_SIZE(json_basekeys));

    // game type is required to identify for which game the addon should be shown in the menu
    addonPtr->gametype = AJ_ParseGameFlag(addonPtr, root, jsonkey_game);
    if (addonPtr->gametype == BASEGAME_NONE)
    {
        LOG_F(ERROR, "No valid game type specified for addon: '%s'! (key: %s)", addonPtr->internalId, jsonkey_game);
        jsonErrorCnt++;
    }

    // creator must specify an identity for the addon, such that other addons can reference it
    if (AJ_ParseIdentity(addonPtr, root, jsonkey_depid) != 0)
    {
        LOG_F(ERROR, "Missing identity for addon: '%s'! (key: %s)", addonPtr->internalId, jsonkey_depid);
        jsonErrorCnt++;
    }

    // game crc (crc match for root parent grp)
    parseResult = AJ_ParseGameCRC(addonPtr, root, jsonkey_gamecrc);
    if (parseResult == -1) jsonErrorCnt++;

    // version string (can be omitted)
    parseResult = AJ_ParseVersion(addonPtr, root, jsonkey_version);
    if (parseResult == -1) jsonErrorCnt++;

    // if the title isn't specified, use the package filename. On error, bail out.
    parseResult = AJ_ParseString(addonPtr, root, jsonkey_title, addonPtr->jsondat.title, ADDON_MAXTITLE);
    if (parseResult == 1)
        Bstrncpy(addonPtr->jsondat.title, packfn, ADDON_MAXTITLE);
    else if (parseResult == -1)
        jsonErrorCnt++;

    // if the author isn't specified, show missing author string. On error, bail out.
    parseResult = AJ_ParseString(addonPtr, root, jsonkey_author, addonPtr->jsondat.author, ADDON_MAXAUTHOR);
    if (parseResult == -1)
        jsonErrorCnt++;

    // if description missing, copy default description.
    parseResult = AJ_ParseDescription(addonPtr, root, jsonkey_desc);
    if (parseResult == -1)
        jsonErrorCnt++;

    // rendmode (can be omitted)
    parseResult = AJ_ParseRendmode(addonPtr, root, jsonkey_rendmodes);
    if (parseResult == -1) jsonErrorCnt++;
    else if (parseResult == 1)
    {
        // compatible with all modes if unspecified
        addonPtr->jsondat.compat_rendmodes = ADDON_RENDMASK;
    }

    // CON script paths (optional)
    parseResult = AJ_ParseScriptModules(addonPtr, root, jsonkey_con, basepath, addonPtr->jsondat.main_script_path,
                                          addonPtr->jsondat.script_modules, addonPtr->jsondat.num_script_modules);
    if (parseResult == -1) jsonErrorCnt++;

    // DEF script paths (optional)
    parseResult = AJ_ParseScriptModules(addonPtr, root, jsonkey_def, basepath, addonPtr->jsondat.main_def_path,
                                          addonPtr->jsondat.def_modules, addonPtr->jsondat.num_def_modules);
    if (parseResult == -1) jsonErrorCnt++;

    // Preview image filepath (optional)
    parseResult = AJ_ParseFilePath(addonPtr, root, jsonkey_image, addonPtr->jsondat.preview_path, basepath);
    if (parseResult == -1) jsonErrorCnt++;

    // RTS file path
    parseResult = AJ_ParseFilePath(addonPtr, root, jsonkey_rts, addonPtr->jsondat.main_rts_path, basepath);
    if (parseResult == -1) jsonErrorCnt++;

    // map to launch after reboot (can be omitted)
    parseResult = AJ_ParseStartMap(addonPtr, root, jsonkey_startmap);
    if (parseResult == -1) jsonErrorCnt++;

    // dependencies
    parseResult = AJ_ParseDependencyList(addonPtr, root, jsonkey_dependencies, addonPtr->jsondat.dependencies, addonPtr->jsondat.num_dependencies);
    if (parseResult == -1) jsonErrorCnt++;

    // incompatibles
    parseResult = AJ_ParseDependencyList(addonPtr, root, jsonkey_incompatibles,
                                                    addonPtr->jsondat.incompatibles, addonPtr->jsondat.num_incompatibles);
    if (parseResult == -1) jsonErrorCnt++;

    sjson_destroy_context(ctx);
    if (jsonErrorCnt > 0)
    {
        LOG_F(ERROR, "Found %d errors in addon descriptor of: '%s'", jsonErrorCnt, addonPtr->internalId);
        return -1;
    }

    return 0;
}

// close recently opened package (removes most recently opened one)
static void Addon_PackageCleanup(const int32_t grpfileidx)
{
    if (grpfileidx < numgroupfiles)
        popgroupfile(); // remove grp/ssi
    else
        popgroupfromkzstack(); // remove zip
}

// Check if the addon directory exists. This is always placed in the folder where the exe is found.
static int32_t Addon_GetLocalDir(char * pathbuf, const int32_t buflen)
{
    char* appdir = Bgetappdir();
    Bsnprintf(pathbuf, buflen, "%s/%s", appdir, addondirname);
    Xfree(appdir);

    if (!buildvfs_isdir(pathbuf))
    {
        // DLOG_F(INFO, "Addon path does not exist: '%s", pathbuf);
        return -1;
    }

    return 0;
}

// remove leading slashes and other non-alpha chars
static char* Addon_CreateInternalIdentity(const char* src, const int32_t srclen)
{
    int i = 0;
    while (i < srclen && !isalpha(src[i])) i++;
    return (i >= srclen) ? nullptr : Xstrdup(&src[i]);
}

// read addon packages (zip, grp, pk3...) from local folder
static void Addon_ReadLocalPackages(fnlist_t* fnlist, const char* addondir)
{
    for (auto & ext : addon_extensions)
    {
        BUILDVFS_FIND_REC *rec;
        fnlist_getnames(fnlist, addondir, ext, -1, 0);
        for (rec=fnlist->findfiles; rec; rec=rec->next)
        {
            char package_path[BMAX_PATH];
            int const nchar = Bsnprintf(package_path, BMAX_PATH, "%s/%s", addondir, rec->name);

            useraddon_t* & addonPtr = s_useraddons[s_numuseraddons] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));
            addonPtr->internalId = Addon_CreateInternalIdentity(package_path, nchar);

            Bstrncpy(addonPtr->data_path, package_path, BMAX_PATH);
            addonPtr->loadorder_idx = DEFAULT_LOADORDER_IDX;

            // set initial file type based on extension
            if (!Bstrcmp(ext, grp_ext)) addonPtr->loadtype = LT_GRP;
            else if (!Bstrcmp(ext, ssi_ext)) addonPtr->loadtype = LT_SSI;
            else addonPtr->loadtype = LT_ZIP;

            // load package contents to access the json and preview within
            const int32_t grpfileidx = initgroupfile(package_path);
            if (grpfileidx == -1)
            {
                DLOG_F(ERROR, "Failed to open addon package at '%s'", package_path);
                addonPtr->freeContents();
                DO_FREE_AND_NULL(addonPtr);
                continue;
            }
            else if (grpfileidx >= numgroupfiles) // zip file renamed to grp
                addonPtr->loadtype = LT_ZIP;


            char json_path[BMAX_PATH];
            Bsnprintf(json_path, BMAX_PATH, "/%s", addonjsonfn);

            if (AJ_ParseJsonDescriptor(json_path, addonPtr, "/", rec->name))
            {
                Addon_PackageCleanup(grpfileidx);
                addonPtr->freeContents();
                DO_FREE_AND_NULL(addonPtr);
                continue;
            }
            Addon_PackageCleanup(grpfileidx);
            addonPtr->setSelected(CONFIG_GetAddonActivationStatus(addonPtr->internalId));
            ++s_numuseraddons;
        }

        fnlist_clearnames(fnlist);
    }
}

// find addons from subfolders
static void Addon_ReadLocalSubfolders(fnlist_t* fnlist, const char* addondir)
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

        useraddon_t* & addonPtr = s_useraddons[s_numuseraddons] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));
        addonPtr->internalId = Addon_CreateInternalIdentity(basepath, nchar);

        Bstrncpy(addonPtr->data_path, basepath, BMAX_PATH);
        addonPtr->loadorder_idx = DEFAULT_LOADORDER_IDX;
        addonPtr->loadtype = LT_FOLDER;

        char json_path[BMAX_PATH];
        Bsnprintf(json_path, BMAX_PATH, "%s/%s", basepath, addonjsonfn);
        if (AJ_ParseJsonDescriptor(json_path, addonPtr, basepath, rec->name))
        {
            addonPtr->freeContents();
            DO_FREE_AND_NULL(addonPtr);
            continue;
        }

        addonPtr->setSelected(CONFIG_GetAddonActivationStatus(addonPtr->internalId));
        ++s_numuseraddons;
    }
    fnlist_clearnames(fnlist);
}

// find addon from Steam Workshop folders
static void Addon_ReadWorkshopItems(void)
{
    // TODO
}

// count potential maximum number of addons
static int32_t Addon_CountPotentialAddons(void)
{
    int32_t numaddons = 0;

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
    g_addoncount_tcs = 0;
    g_addoncount_mods = 0;
    for (int i = 0; i < s_numuseraddons; i++)
    {
        useraddon_t* addonPtr = s_useraddons[i];
        if (!addonPtr->isValid()) continue;
        else if (addonPtr->isTotalConversion()) g_addoncount_tcs++;
        else g_addoncount_mods++;
    }

    g_useraddons_tcs = (useraddon_t **) Xcalloc(g_addoncount_tcs, sizeof(useraddon_t*));
    g_useraddons_mods = (useraddon_t **) Xcalloc(g_addoncount_mods, sizeof(useraddon_t*));

    //copy data over
    int tcidx = 0, modidx = 0;
    for (int i = 0; i < s_numuseraddons; i++)
    {
        useraddon_t* addonPtr = s_useraddons[i];
        if (!addonPtr->isValid())
        {
            DO_FREE_AND_NULL(s_useraddons[i]);
            continue;
        }
        else if (addonPtr->isTotalConversion()) g_useraddons_tcs[tcidx++] = addonPtr;
        else g_useraddons_mods[modidx++] = addonPtr;
    }
}

void Addon_FreeUserTCs(void)
{
    for_tcaddons(addonPtr, { addonPtr->freeContents(); Xfree(addonPtr); });
    DO_FREE_AND_NULL(g_useraddons_tcs);
    g_addoncount_tcs = 0;
}

void Addon_FreeUserMods(void)
{
    for_modaddons(addonPtr, { addonPtr->freeContents(); Xfree(addonPtr); });
    DO_FREE_AND_NULL(g_useraddons_mods);
    g_addoncount_mods = 0;
}

// Important: this function is called before the setup window is shown
// Hence it must not depend on any variables initialized from game content
void Addon_ReadJsonDescriptors(void)
{
    // free previous storage
    Addon_FreeUserTCs();
    Addon_FreeUserMods();

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

    char addonpathbuf[BMAX_PATH];
    if (!Addon_GetLocalDir(addonpathbuf, BMAX_PATH))
    {
        fnlist_t fnlist = FNLIST_INITIALIZER;
        fnlist_clearnames(&fnlist);
        Addon_ReadLocalPackages(&fnlist, addonpathbuf);
        Addon_ReadLocalSubfolders(&fnlist, addonpathbuf);
    }

    Addon_ReadWorkshopItems();

    pathsearchmode = bakpathsearchmode;

    if (s_numuseraddons > 0)
        Addon_SplitAddonTypes();

    DO_FREE_AND_NULL(s_useraddons);
    s_numuseraddons = 0;
}
