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
#include "addonjson.h"
#include "sjson.h"

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

static const char missing_author[] = "N/A";
static const char missing_description[] = "No description available.";

// utility to check for file existence
int32_t Addon_CheckFilePresence(const char* filepath)
{
    buildvfs_kfd jsonfil = kopen4load(filepath, 0);
    if (jsonfil != buildvfs_kfd_invalid)
    {
        kclose(jsonfil);
        return 0;
    }

    return -1;
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

// This awful function verifies that version strings adhere to the following format:
// REGEX: ((([1-9][0-9]*)|0)\.)*(([1-9][0-9]*))(\-[a-zA-Z0-9]+)?
// valid examples: {1.0, 1.0.3.4-alphanum123, 2, 4.0-a}
// This would be better accomplished with a regular expression library
int32_t Addon_CheckVersionFormat(const char* versionString)
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

// compares two version strings. Outputs 0 if equal, 1 if versionA is greater, -1 if versionB is greater.
int32_t Addon_CompareVersionStrings(const char* versionA, const char* versionB)
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

static void Addon_ParseJson_VerifyKeys(const char* json_fn, sjson_node *node, const char* parentkey,
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

        Addon_ParseJson_VerifyKeys(addonPtr->internalId, snode, key, json_scriptkeys, ARRAY_SIZE(json_scriptkeys));

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

        Addon_ParseJson_VerifyKeys(addonPtr->internalId, snode, key, json_dependencykeys, ARRAY_SIZE(json_dependencykeys));

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

    Addon_ParseJson_VerifyKeys(addonPtr->internalId, ele, key, json_startmapkeys, ARRAY_SIZE(json_startmapkeys));

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

static int32_t Addon_SetRendmodeFromString(useraddon_t* addonPtr, const char* rmodestr)
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
static int32_t Addon_ParseJson_Rendmode(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    addonPtr->jsondat.compat_rendmodes = ADDON_RENDNONE;
    sjson_node * ele = sjson_find_member_nocase(root, key);
    if (ele == nullptr) return 1;

    if (ele->tag == SJSON_STRING)
    {
        return Addon_SetRendmodeFromString(addonPtr, ele->string_);
    }
    else if (ele->tag == SJSON_ARRAY)
    {
        sjson_node *child;
        sjson_foreach(child, ele)
        {
            if (child->tag != SJSON_STRING || Addon_SetRendmodeFromString(addonPtr, child->string_))
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
int32_t Addon_ParseJson(char* json_fn, useraddon_t* addonPtr, const char* basepath, const char* packfn)
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

    Addon_ParseJson_VerifyKeys(addonPtr->internalId, root, nullptr, json_basekeys, ARRAY_SIZE(json_basekeys));

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
    parseResult = Addon_ParseJson_Rendmode(addonPtr, root, jsonkey_rendmodes);
    if (parseResult == -1) jsonErrorCnt++;
    else if (parseResult == 1)
    {
        // compatible with all modes if unspecified
        addonPtr->jsondat.compat_rendmodes = ADDON_RENDMASK;
    }

    // CON script paths (optional)
    parseResult = Addon_ParseJson_Scripts(addonPtr, root, jsonkey_con, basepath, addonPtr->jsondat.main_script_path,
                                          addonPtr->jsondat.script_modules, addonPtr->jsondat.num_script_modules);
    if (parseResult == -1) jsonErrorCnt++;

    // DEF script paths (optional)
    parseResult = Addon_ParseJson_Scripts(addonPtr, root, jsonkey_def, basepath, addonPtr->jsondat.main_def_path,
                                          addonPtr->jsondat.def_modules, addonPtr->jsondat.num_def_modules);
    if (parseResult == -1) jsonErrorCnt++;

    // Preview image filepath (optional)
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

    sjson_destroy_context(ctx);
    if (jsonErrorCnt > 0)
    {
        LOG_F(ERROR, "Found %d errors in addon descriptor of: '%s'", jsonErrorCnt, addonPtr->internalId);
        return -1;
    }

    return 0;
}