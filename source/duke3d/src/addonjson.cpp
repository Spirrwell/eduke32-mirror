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

// all keys of the base json level -- anything else being present triggers a warning
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

// string sequences to identify different gametypes -- anything else triggers an error
static const char jsonval_gt_any[] = "any";
static const char jsonval_gt_duke[] = "duke3d";
static const char jsonval_gt_nam[] = "nam";
static const char jsonval_gt_ww2gi[] = "ww2gi";
static const char jsonval_gt_fury[] = "fury";

// string sequences to identify script type
static const char jsonval_scriptmain[] = "main";
static const char jsonval_scriptmodule[] = "module";

// rendmode types -- opengl is either polymost or polymer
static const char jsonval_rendmode_classic[] = "classic";
static const char jsonval_rendmode_opengl[] = "opengl";
static const char jsonval_rendmode_polymost[] = "polymost";
static const char jsonval_rendmode_polymer[] = "polymer";

// utility to check for file existence
static int32_t AddonJson_CheckFilePresence(const char* filepath)
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
static int32_t AddonJson_CheckVersionFormat(const char* versionString)
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
            DLOG_F(ERROR, "Version grouping '%s' starts with invalid char '%c'", versionString, first);
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
                DLOG_F(ERROR, "Non-digit '%c' found in version grouping of '%s'", segChar, versionString);
                return -1;
            }
        }
    }

    // must not end with period
    if (segChar == '.')
    {
        DLOG_F(ERROR, "Version string '%s' cannot end with period char!", versionString);
        return -1;
    }

    // allow arbitrary alpha-numerical string after dash
    if (segChar == '-')
    {
        if (!versionString[k])
        {
            DLOG_F(ERROR, "No characters following dash in version string '%s'!", versionString);
            return -1;
        }

        while ((segChar = versionString[k++]))
        {
            if (!isalnum(segChar))
            {
                DLOG_F(ERROR, "Non-alphanum char found after dash: %c in version string '%s'", segChar, versionString);
                return -1;
            }
        }
    }

    return 0;
}

// Check if any unknown keys are present in the given sjson_node, using the provided array of keys
static void AddonJson_CheckUnknownKeys(const char* json_fn, sjson_node *node, const char* parentkey,
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

// check whether sjson node has string typed value contents, return -1 and report error if not
static int32_t AddonJson_CheckStringTyped(const useraddon_t *addonPtr, sjson_node *ele, const char *key)
{
    if (ele->tag != SJSON_STRING)
    {
        LOG_F(ERROR, "Addon descriptor member '%s' of addon '%s' is not string typed!", key, addonPtr->internalId);
        return -1;
    }
    return 0;
}

// parse arbitrary sjson string content and replace given reference pointer (nulled if key not found)
static int32_t AddonJson_ParseString(useraddon_t *addonPtr, sjson_node *root, const char *key, char* & dstPtr)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    dstPtr = nullptr;

    if (ele == nullptr) return 1;
    else if (AddonJson_CheckStringTyped(addonPtr, ele, key)) return -1;

    dstPtr = Xstrdup(ele->string_);
    return 0;
}

// treat sjson string content as a file path and check for file presence, otherwise treat same as string
static int32_t AddonJson_ParseFilePath(useraddon_t* addonPtr, sjson_node* root, const char *key, char* & dstPtr, const char* basepath)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    dstPtr = nullptr;

    if (ele == nullptr) return 1;
    else if (AddonJson_CheckStringTyped(addonPtr, ele, key)) return -1;

    Bsnprintf(tempbuf, BMAX_PATH, "%s/%s", basepath, ele->string_);
    if (AddonJson_CheckFilePresence(tempbuf))
    {
        //TODO: Need to verify whether this actually works properly with Ken GRP/SSI/ZIP files
        LOG_F(ERROR, "Preview image for addon '%s' at location '%s' does not exist!", addonPtr->internalId, tempbuf);
        return -1;
    }

    dstPtr = Xstrdup(tempbuf);
    Bcorrectfilename(dstPtr, 0);
    return 0;
}

// return 0 if given string satisfies the restrictions set on external identity
// TODO: currently on disallows whitespace and empty strings, maybe increase restriction?
static int32_t AddonJson_CheckExternalIdentityRestrictions(const useraddon_t *addonPtr, const char* ident)
{
    if (!ident || !ident[0])
    {
        LOG_F(ERROR, "Identity string of addon %s cannot be empty!", addonPtr->internalId);
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

// get external identity used for dependency references, and check format
static int32_t AddonJson_ParseExternalId(useraddon_t *addonPtr, sjson_node *root, const char *key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->externalId = nullptr;

    if (ele == nullptr) return 1;
    else if (AddonJson_CheckStringTyped(addonPtr, ele, key) || AddonJson_CheckExternalIdentityRestrictions(addonPtr, ele->string_))
        return -1;

    addonPtr->externalId = Xstrdup(ele->string_);
    return 0;
}

// retrieve version string and check format
static int32_t AddonJson_ParseVersion(useraddon_t *addonPtr, sjson_node *root, const char *key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->version = nullptr;

    if (ele == nullptr) return 1;
    else if (AddonJson_CheckStringTyped(addonPtr, ele, key)) return -1;

    addonPtr->version = Xstrdup(ele->string_);
    if (AddonJson_CheckVersionFormat(addonPtr->version))
    {
        LOG_F(ERROR, "Version string '%s' of addon %s has incorrect format!", addonPtr->version, addonPtr->internalId);
        DO_FREE_AND_NULL(addonPtr->version);
        return -1;
    }
    return 0;
}

// parse and store script file paths, check for file presence and other errors
static int32_t AddonJson_ParseScriptModules(useraddon_t *addonPtr, sjson_node* root, const char* key, const char* basepath,
                                        char* & mscriptPtr, char** & modulebuffer, int32_t & modulecount)
{
    mscriptPtr = nullptr;
    modulebuffer = nullptr;
    modulecount = 0;

    sjson_node * nodes = sjson_find_member_nocase(root, key);
    if (nodes == nullptr) return 1;

    // TODO: Allow single object without array, see rendmode
    if (nodes->tag != SJSON_ARRAY)
    {
        LOG_F(ERROR, "Value of key '%s' of addon '%s' must be an array!", key, addonPtr->internalId);
        return -1;
    }

    int const numchildren = sjson_child_count(nodes);
    modulebuffer = (char **) Xmalloc(numchildren * sizeof(char*));

    sjson_node *snode, *script_path, *script_type;
    bool hasError = false;
    int32_t numvalidchildren = 0;
    sjson_foreach(snode, nodes)
    {
        if (snode->tag != SJSON_OBJECT)
        {
            LOG_F(ERROR, "Invalid type found in array of member '%s' of addon '%s'!", key, addonPtr->internalId);
            hasError = true;
            continue;
        }

        AddonJson_CheckUnknownKeys(addonPtr->internalId, snode, key, json_scriptkeys, ARRAY_SIZE(json_scriptkeys));

        script_path = sjson_find_member_nocase(snode, jsonkey_scriptpath);
        if (script_path == nullptr || script_path->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Script path of key %s missing or has invalid format in addon '%s'!", key, addonPtr->internalId);
            hasError = true;
            continue;
        }

        Bsnprintf(tempbuf, BMAX_PATH, "%s/%s", basepath, script_path->string_);
        if (AddonJson_CheckFilePresence(tempbuf))
        {
            //TODO: Need to verify whether this actually works properly with Ken GRP/SSI/ZIP files
            LOG_F(ERROR, "Script file of addon '%s' at location '%s' does not exist!", addonPtr->internalId, tempbuf);
            hasError = true;
            continue;
        }

        script_type = sjson_find_member_nocase(snode, jsonkey_scripttype);
        if (script_type == nullptr || script_type->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Script type of key %s missing or has invalid format in addon '%s'!", key, addonPtr->internalId);
            hasError = true;
            continue;
        }

        if (!Bstrncasecmp(script_type->string_, jsonval_scriptmain, ARRAY_SIZE(jsonval_scriptmain)))
        {
            if (mscriptPtr)
            {
                LOG_F(ERROR, "More than one main '%s' script specified in addon '%s'!", key, addonPtr->internalId);
                hasError = true;
                continue;
            }
            mscriptPtr = Xstrdup(script_path->string_);
        }
        else if (!Bstrncasecmp(script_type->string_, jsonval_scriptmodule, ARRAY_SIZE(jsonval_scriptmodule)))
        {
            modulebuffer[numvalidchildren] = Xstrdup(script_path->string_);
            numvalidchildren++;
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
        DO_FREE_AND_NULL(mscriptPtr);
        for (int i = 0; i < numvalidchildren; i++)
            Xfree(modulebuffer[i]);
        numvalidchildren = 0;
    }

    // valid children may be zero from error or no modules specified
    if (numvalidchildren == 0)
    {
        DO_FREE_AND_NULL(modulebuffer);
        modulecount = 0;
        return (hasError) ? -1 : 0;
    }
    else
    {
        modulebuffer = (char **) Xrealloc(modulebuffer, numvalidchildren * sizeof(char*));
        modulecount = numvalidchildren;
        return 0;
    }
}

// the version string in the dependency portion is prepended with comparison characters
static int32_t AddonJson_SetupDependencyVersion(addondependency_t * dep, const char* versionString)
{
    dep->version = nullptr;
    dep->cOp = AVCOMP_NOOP;
    if (versionString == nullptr || !versionString[0])
        return 1;

    int k = 0;
    switch(versionString[0])
    {
        case '=':
            k++;
            if (versionString[1] == '=') { dep->cOp = AVCOMP_EQ; k++; }
            else
            {
                LOG_F(ERROR, "Version string '%s' has incorrect format!", dep->version);
                return -1;
            }
            break;
        case '>':
            k++;
            if (versionString[1] == '=') { dep->cOp = AVCOMP_GTEQ; k++; }
            else dep->cOp = AVCOMP_GT;
            break;
        case '<':
            k++; if (versionString[1] == '=') { dep->cOp = AVCOMP_LTEQ; k++; }
            else dep->cOp = AVCOMP_LT;
            break;
        default:
            // assume equality
            dep->cOp = AVCOMP_EQ;
            break;
    }

    dep->version = Xstrdup(&versionString[k]);
    if (AddonJson_CheckVersionFormat(dep->version))
    {
        LOG_F(ERROR, "Version string '%s' has incorrect format!", dep->version);
        DO_FREE_AND_NULL(dep->version);
        dep->cOp = AVCOMP_NOOP;
        return -1;
    }

    return 0;
}

// get the dependency property
static int32_t AddonJson_ParseDependencyList(useraddon_t* addonPtr, sjson_node* root, const char* key,
                                          addondependency_t*& dep_ptr, int32_t& num_valid_deps)
{
    dep_ptr = nullptr;
    num_valid_deps = 0;

    sjson_node * nodes = sjson_find_member_nocase(root, key);
    if (nodes == nullptr) return 1;

    // TODO: Allow single object without array, see rendmode
    if (nodes->tag != SJSON_ARRAY)
    {
        LOG_F(ERROR, "Content of member '%s' of addon '%s' is not an array!", key, addonPtr->internalId);
        return -1;
    }

    int const numchildren = sjson_child_count(nodes);
    dep_ptr = (addondependency_t *) Xcalloc(numchildren, sizeof(addondependency_t));

    sjson_node *snode;
    sjson_node *dep_uid, *dep_version;
    sjson_foreach(snode, nodes)
    {
        if (snode->tag != SJSON_OBJECT)
        {
            LOG_F(ERROR, "Invalid type found in array of member '%s' of addon '%s'!", key, addonPtr->internalId);
            continue;
        }

        AddonJson_CheckUnknownKeys(addonPtr->internalId, snode, key, json_dependencykeys, ARRAY_SIZE(json_dependencykeys));

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
        if (AddonJson_CheckExternalIdentityRestrictions(addonPtr, dep_uid->string_))
            continue;

        // only bail if string specified and invalid, otherwise accept dependencies without version
        adt.dependencyId = Xstrdup(dep_uid->string_);
        if (dep_version && AddonJson_SetupDependencyVersion(&adt, dep_version->string_))
        {
            LOG_F(ERROR, "Invalid version string for dependency '%s' in addon: %s!", adt.dependencyId, addonPtr->internalId);
            DO_FREE_AND_NULL(adt.dependencyId);
            continue;
        }

        num_valid_deps++;
    }

    if (num_valid_deps < numchildren)
    {
        for (int i = 0; i < num_valid_deps; i++)
            dep_ptr[i].cleanup();
        DO_FREE_AND_NULL(dep_ptr);
        num_valid_deps = 0;
        return -1;
    }

    dep_ptr = (addondependency_t *) Xrealloc(dep_ptr, num_valid_deps * sizeof(addondependency_t));
    return 0;
}

// game type for which the addon is valid and available
static int32_t AddonJson_ParseGameFlag(const useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    if (ele == nullptr || AddonJson_CheckStringTyped(addonPtr, ele, key))
        return ADDONGF_NONE;

    if (!Bstrncasecmp(ele->string_, jsonval_gt_any, ARRAY_SIZE(jsonval_gt_any)))
        return ADDONGF_ANY;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_duke, ARRAY_SIZE(jsonval_gt_duke)))
        return ADDONGF_DUKE;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_fury, ARRAY_SIZE(jsonval_gt_fury)))
        return ADDONGF_FURY;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_ww2gi, ARRAY_SIZE(jsonval_gt_ww2gi)))
        return ADDONGF_WW2GI;
    else if (!Bstrncasecmp(ele->string_, jsonval_gt_nam, ARRAY_SIZE(jsonval_gt_nam)))
        return ADDONGF_NAM;
    else
    {
        LOG_F(ERROR, "Invalid gametype on addon '%s'.\nValid gametype strings are: {%s, %s, %s, %s, %s}.",
                addonPtr->internalId, jsonval_gt_any, jsonval_gt_duke, jsonval_gt_nam, jsonval_gt_ww2gi, jsonval_gt_fury);
        return ADDONGF_NONE;
    }
}

// The gameCRC acts as an additional method to finegrain control for which game the addon should show up.
static int32_t AddonJson_ParseGameCRC(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->gamecrc = 0;

    if (ele == nullptr) return 1;
    else if (AddonJson_CheckStringTyped(addonPtr, ele, key)) return -1;

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

static int32_t AddonJson_ParseStartMap(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    addonPtr->startmapfilename = nullptr;
    addonPtr->startlevel = addonPtr->startvolume = 0;
    addonPtr->aflags &= ~ADDONFLAG_STARTMAP;

    if (ele == nullptr) return 1;
    else if (ele->tag != SJSON_OBJECT)
    {
        LOG_F(ERROR, "Value for key '%s' of addon %s must be an object!", key, addonPtr->internalId);
        return -1;
    }

    AddonJson_CheckUnknownKeys(addonPtr->internalId, ele, key, json_startmapkeys, ARRAY_SIZE(json_startmapkeys));

    sjson_node* ele_mapfile = sjson_find_member_nocase(ele, jsonkey_mapfile);
    if (ele_mapfile)
    {
        if (AddonJson_CheckStringTyped(addonPtr, ele_mapfile, jsonkey_mapfile))
            return -1;

        // do not check for file existence
        addonPtr->startmapfilename = Xstrdup(ele_mapfile->string_);
        Bcorrectfilename(addonPtr->startmapfilename, 0);

        addonPtr->startlevel = 7;
        addonPtr->startvolume = 0;
        addonPtr->aflags |= ADDONFLAG_STARTMAP;
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

        addonPtr->startlevel = ele_maplevel->number_;
        addonPtr->startvolume = ele_mapvolume->number_;
        addonPtr->aflags |= ADDONFLAG_STARTMAP;
        return 0;
    }
    else
    {
        LOG_F(ERROR, "Invalid startmap structure for addon %s!", addonPtr->internalId);
        LOG_F(INFO, "Valid keys are: {\"%s\", \"%s\", \"%s\"}", jsonkey_maplevel, jsonkey_mapvolume, jsonkey_mapfile);
        return -1;
    }
}

// set rendmode using extracted string
static int32_t AddonJson_SetRendmodeFromString(useraddon_t* addonPtr, const char* rmodestr)
{
    if (!Bstrncasecmp(rmodestr, jsonval_rendmode_classic, ARRAY_SIZE(jsonval_rendmode_classic)))
        addonPtr->compatrendmode |= ADDONRM_CLASSIC;
    else if (!Bstrncasecmp(rmodestr, jsonval_rendmode_opengl, ARRAY_SIZE(jsonval_rendmode_opengl)))
        addonPtr->compatrendmode |= (ADDONRM_POLYMOST | ADDONRM_POLYMER);
    else if (!Bstrncasecmp(rmodestr, jsonval_rendmode_polymost, ARRAY_SIZE(jsonval_rendmode_polymost)))
        addonPtr->compatrendmode |= ADDONRM_POLYMOST;
    else if (!Bstrncasecmp(rmodestr, jsonval_rendmode_polymer, ARRAY_SIZE(jsonval_rendmode_polymer)))
        addonPtr->compatrendmode |= ADDONRM_POLYMER;
    else
    {
        LOG_F(ERROR, "Unknown rendmode '%s' in addon '%s'!", rmodestr, addonPtr->internalId);
        return -1;
    }

    return 0;
}

// retrieve rendmode string (or list) and set rendmode from it
static int32_t AddonJson_ParseRendmode(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    addonPtr->compatrendmode = ADDONRM_NONE;
    sjson_node * ele = sjson_find_member_nocase(root, key);
    if (ele == nullptr) return 1;

    if (ele->tag == SJSON_STRING)
    {
        return AddonJson_SetRendmodeFromString(addonPtr, ele->string_);
    }
    else if (ele->tag == SJSON_ARRAY)
    {
        sjson_node *child;
        sjson_foreach(child, ele)
        {
            if (child->tag != SJSON_STRING || AddonJson_SetRendmodeFromString(addonPtr, child->string_))
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
static int32_t AddonJson_ParseDescriptor(sjson_context *ctx, char* json_fn, useraddon_t* addonPtr, const char* basepath, const char* packfn)
{
    // open json descriptor (try 8.3 format as well, due to ken grp restrictions)
    const bool isgroup = addonPtr->package_type & (ADDONLT_ZIP | ADDONLT_GRP | ADDONLT_SSI);
    buildvfs_kfd jsonfil = kopen4load(json_fn, (isgroup ? 2 : 0));
    if (jsonfil == buildvfs_kfd_invalid)
    {
        json_fn[strlen(json_fn) - 1] = '\0';
        jsonfil = kopen4load(json_fn, (isgroup ? 2 : 0));
        if (jsonfil == buildvfs_kfd_invalid)
            return 1; // not found, is not an addon
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
    sjson_reset_context(ctx);
    if (!sjson_validate(ctx, jsonTextBuf))
    {
        LOG_F(ERROR, "Structural syntax errors detected in addon descriptor file '%s'!", json_fn);
        return -1;
    }

    int32_t parseResult, jsonErrorCnt = 0;
    sjson_node * root = sjson_decode(ctx, jsonTextBuf);
    Xfree(jsonTextBuf);

    AddonJson_CheckUnknownKeys(addonPtr->internalId, root, nullptr, json_basekeys, ARRAY_SIZE(json_basekeys));

    // game type is required to identify for which game the addon should be shown in the menu (required)
    addonPtr->gametype = AddonJson_ParseGameFlag(addonPtr, root, jsonkey_game);
    if (addonPtr->gametype == ADDONGF_NONE)
    {
        LOG_F(ERROR, "No valid game type specified for addon: '%s'! (key: %s)", addonPtr->internalId, jsonkey_game);
        jsonErrorCnt++;
    }

    // creator must specify an identity for the addon, such that other addons can reference it (required)
    if (AddonJson_ParseExternalId(addonPtr, root, jsonkey_depid) != 0)
    {
        LOG_F(ERROR, "Missing identity for addon: '%s'! (key: %s)", addonPtr->internalId, jsonkey_depid);
        jsonErrorCnt++;
    }

    // game crc (crc match for root parent grp)  (optional)
    parseResult = AddonJson_ParseGameCRC(addonPtr, root, jsonkey_gamecrc);
    if (parseResult == -1) jsonErrorCnt++;

    // version string  (optional)
    parseResult = AddonJson_ParseVersion(addonPtr, root, jsonkey_version);
    if (parseResult == -1) jsonErrorCnt++;

    // title  (optional) -- use package filename if unspecified
    parseResult = AddonJson_ParseString(addonPtr, root, jsonkey_title, addonPtr->title);
    if (parseResult == 1)
        addonPtr->title = Xstrdup(packfn);
    else if (parseResult == -1)
        jsonErrorCnt++;

    // author of addon (optional)
    parseResult = AddonJson_ParseString(addonPtr, root, jsonkey_author, addonPtr->author);
    if (parseResult == -1)
        jsonErrorCnt++;

    // description for addon (optional)
    parseResult = AddonJson_ParseString(addonPtr, root, jsonkey_desc, addonPtr->description);
    if (parseResult == -1)
        jsonErrorCnt++;

    // rendmode (optional)
    parseResult = AddonJson_ParseRendmode(addonPtr, root, jsonkey_rendmodes);
    if (parseResult == 1)
        // compatible with all modes if unspecified
        addonPtr->compatrendmode = ADDONRM_MASK;
    else if (parseResult == -1)
        jsonErrorCnt++;

    // CON script paths (optional)
    parseResult = AddonJson_ParseScriptModules(addonPtr, root, jsonkey_con, basepath,
                    addonPtr->mscript_path, addonPtr->script_modules, addonPtr->num_script_modules);
    if (parseResult == -1) jsonErrorCnt++;

    // DEF script paths (optional)
    parseResult = AddonJson_ParseScriptModules(addonPtr, root, jsonkey_def, basepath,
                    addonPtr->mdef_path, addonPtr->def_modules, addonPtr->num_def_modules);
    if (parseResult == -1) jsonErrorCnt++;

    // Preview image filepath (optional)
    parseResult = AddonJson_ParseFilePath(addonPtr, root, jsonkey_image, addonPtr->preview_path, basepath);
    if (parseResult == -1) jsonErrorCnt++;

    // RTS file path (optional)
    // TODO: Check this again (bugged)
    parseResult = AddonJson_ParseFilePath(addonPtr, root, jsonkey_rts, addonPtr->mrts_path, basepath);
    if (parseResult == -1) jsonErrorCnt++;

    // map to launch after reboot (optional)
    parseResult = AddonJson_ParseStartMap(addonPtr, root, jsonkey_startmap);
    if (parseResult == -1) jsonErrorCnt++;

    // dependencies (optional)
    parseResult = AddonJson_ParseDependencyList(addonPtr, root, jsonkey_dependencies, addonPtr->dependencies, addonPtr->num_dependencies);
    if (parseResult == -1) jsonErrorCnt++;

    // incompatibles (optional)
    parseResult = AddonJson_ParseDependencyList(addonPtr, root, jsonkey_incompatibles, addonPtr->incompatibles, addonPtr->num_incompatibles);
    if (parseResult == -1) jsonErrorCnt++;

    if (jsonErrorCnt > 0)
    {
        LOG_F(ERROR, "Found %d errors in addon descriptor of: '%s'", jsonErrorCnt, addonPtr->internalId);
        return -1;
    }

    return 0;
}

// to be used after the json is parsed. Sets the content type using the addon contents.
static void AddonJson_SetContentType(useraddon_t* addonPtr)
{
    if (addonPtr->mscript_path || addonPtr->mdef_path)
        addonPtr->content_type = ADDONTYPE_TC;
    else
        addonPtr->content_type = ADDONTYPE_MOD;
}

// Check if the addon directory exists. This is always placed in the folder where the exe is found.
static int32_t AddonJson_GetLocalDir(char * pathbuf, const int32_t buflen)
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

// remove leading slashes and other non-alpha chars from string, copy the string
static char* AddonJson_DupString_RemoveLeadingNonAlpha(const char* src, const int32_t srclen)
{
    //TODO: Replace with a better approach -- this is used for internal ID generation
    int i = 0;
    while (i < srclen && !isalpha(src[i])) i++;
    return (i >= srclen) ? nullptr : Xstrdup(&src[i]);
}

// read addon packages (zip, grp, pk3...) from local folder
static void AddonJson_ReadLocalPackages(sjson_context *ctx, fnlist_t* fnlist, const char* addondir)
{
    for (auto & ext : addon_extensions)
    {
        BUILDVFS_FIND_REC *rec;
        fnlist_getnames(fnlist, addondir, ext, -1, 0);
        for (rec=fnlist->findfiles; rec; rec=rec->next)
        {
            char package_path[BMAX_PATH];
            int const nchar = Bsnprintf(package_path, BMAX_PATH, "%s/%s", addondir, rec->name);

            // absolutely MUST be zero initialized
            useraddon_t* & addonPtr = s_useraddons[s_numuseraddons] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));

            // TODO: May want to change internal ID to just the filename, not the whole path
            addonPtr->internalId = AddonJson_DupString_RemoveLeadingNonAlpha(package_path, nchar);

            // set data path and default loadorder index
            addonPtr->data_path = Xstrdup(package_path);
            addonPtr->loadorder_idx = DEFAULT_LOADORDER_IDX;

            // set initial file type based on extension
            if (!Bstrcmp(ext, grp_ext)) addonPtr->package_type = ADDONLT_GRP;
            else if (!Bstrcmp(ext, ssi_ext)) addonPtr->package_type = ADDONLT_SSI;
            else addonPtr->package_type = ADDONLT_ZIP;

            // load package contents to access the json and preview within
            const int32_t grpfileidx = initgroupfile(package_path);
            if (grpfileidx == -1)
            {
                DLOG_F(ERROR, "Failed to open addon package at '%s'", package_path);
                addonPtr->cleanup();
                DO_FREE_AND_NULL(addonPtr);
                continue;
            }
            // here we have a renamed zip file
            else if (grpfileidx >= numgroupfiles)
                addonPtr->package_type = ADDONLT_ZIP;

            // generate path to json descriptor
            char json_path[BMAX_PATH];
            Bsnprintf(json_path, BMAX_PATH, "/%s", addonjsonfn);

            // parse the json, cleanup
            int parsingFailed = AddonJson_ParseDescriptor(ctx, json_path, addonPtr, "/", rec->name);
            if (grpfileidx < numgroupfiles) popgroupfile();
            else popgroupfromkzstack();

            if (parsingFailed)
            {
                addonPtr->cleanup();
                DO_FREE_AND_NULL(addonPtr);
                continue;
            }
            AddonJson_SetContentType(addonPtr);
            addonPtr->setSelected(CONFIG_GetAddonActivationStatus(addonPtr->internalId));
            ++s_numuseraddons;
        }
        fnlist_clearnames(fnlist);
    }
}

// find addons from subfolders contained within the local addon directory
static void AddonJson_ReadLocalSubfolders(sjson_context *ctx, fnlist_t* fnlist, const char* addondir)
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

        // absolutely MUST be zero initialized
        useraddon_t* & addonPtr = s_useraddons[s_numuseraddons] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));

        // TODO: May want to change internal ID to just the subfolder name, not the whole path
        addonPtr->internalId = AddonJson_DupString_RemoveLeadingNonAlpha(basepath, nchar);

        addonPtr->data_path = Xstrdup(basepath);
        addonPtr->loadorder_idx = DEFAULT_LOADORDER_IDX;
        addonPtr->package_type = ADDONLT_FOLDER;

        char json_path[BMAX_PATH];
        Bsnprintf(json_path, BMAX_PATH, "%s/%s", basepath, addonjsonfn);
        if (AddonJson_ParseDescriptor(ctx, json_path, addonPtr, basepath, rec->name))
        {
            addonPtr->cleanup();
            DO_FREE_AND_NULL(addonPtr);
            continue;
        }

        // on success, set content type and check if addon active
        AddonJson_SetContentType(addonPtr);
        addonPtr->setSelected(CONFIG_GetAddonActivationStatus(addonPtr->internalId));
        ++s_numuseraddons;
    }
    fnlist_clearnames(fnlist);
}

// find addon from Steam Workshop folders (may be scattered)
static void AddonJson_ReadWorkshopItems(void)
{
    // TODO
}

// count potential maximum number of addons
static int32_t AddonJson_CountPotentialAddons(void)
{
    int32_t numaddons = 0;

    char addonpathbuf[BMAX_PATH];
    if (!AddonJson_GetLocalDir(addonpathbuf, BMAX_PATH))
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

// This splits the internal addon array into the distinct types, and check validity of all addons
static void AddonJson_SplitAddonTypes(void)
{
    g_addoncount_tcs = 0;
    g_addoncount_mods = 0;
    for (int i = 0; i < s_numuseraddons; i++)
    {
        useraddon_t* addonPtr = s_useraddons[i];
        if (!addonPtr->isValid())
        {
            DO_FREE_AND_NULL(s_useraddons[i]);
            continue;
        }

        switch (addonPtr->content_type)
        {
            case ADDONTYPE_TC: g_addoncount_tcs++; break;
            case ADDONTYPE_MOD: g_addoncount_mods++; break;
            default:
                LOG_F(ERROR, "Invalid addon type %d for %s, this should never happen.", addonPtr->content_type, addonPtr->internalId);
                break;
        }
    }

    // now we know the counts
    g_useraddons_tcs = (useraddon_t **) Xmalloc(g_addoncount_tcs * sizeof(useraddon_t*));
    g_useraddons_mods = (useraddon_t **) Xmalloc(g_addoncount_mods * sizeof(useraddon_t*));

    //copy data over
    int tcidx = 0, modidx = 0;
    for (int i = 0; i < s_numuseraddons; i++)
    {
        useraddon_t* addonPtr = s_useraddons[i];
        if (!addonPtr) continue;

        switch (addonPtr->content_type)
        {
            case ADDONTYPE_TC: g_useraddons_tcs[tcidx++] = addonPtr; break;
            case ADDONTYPE_MOD: g_useraddons_mods[modidx++] = addonPtr; break;
            default:
                LOG_F(ERROR, "Invalid addon type %d for %s, this should never happen.", addonPtr->content_type, addonPtr->internalId);
                break;
        }
    }
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
    int32_t maxaddons = AddonJson_CountPotentialAddons();
    if (maxaddons <= 0) return;

    // these are updated inside the following functions
    s_useraddons = (useraddon_t **) Xcalloc(maxaddons, sizeof(useraddon_t*));
    s_numuseraddons = 0;

    // context is reused
    sjson_context *ctx = sjson_create_context(0, 0, nullptr);

    char addonpathbuf[BMAX_PATH];
    if (!AddonJson_GetLocalDir(addonpathbuf, BMAX_PATH))
    {
        fnlist_t fnlist = FNLIST_INITIALIZER;
        fnlist_clearnames(&fnlist);
        AddonJson_ReadLocalPackages(ctx, &fnlist, addonpathbuf);
        AddonJson_ReadLocalSubfolders(ctx, &fnlist, addonpathbuf);
    }

    // workshop items are outside the local directory
    AddonJson_ReadWorkshopItems();

    // cleanup
    sjson_destroy_context(ctx);
    pathsearchmode = bakpathsearchmode;

    if (s_numuseraddons > 0)
        AddonJson_SplitAddonTypes();

    DO_FREE_AND_NULL(s_useraddons);
    s_numuseraddons = 0;
}

void Addon_FreeUserTCs(void)
{
    for_tcaddons(addonPtr, { addonPtr->cleanup(); Xfree(addonPtr); });
    DO_FREE_AND_NULL(g_useraddons_tcs);
    g_addoncount_tcs = 0;
}

void Addon_FreeUserMods(void)
{
    for_modaddons(addonPtr, { addonPtr->cleanup(); Xfree(addonPtr); });
    DO_FREE_AND_NULL(g_useraddons_mods);
    g_addoncount_mods = 0;
}