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
static const char jsonkey_grpdata[] = "GRP";
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
    jsonkey_depid, jsonkey_game, jsonkey_gamecrc, jsonkey_version,jsonkey_title,jsonkey_author,
    jsonkey_desc, jsonkey_image, jsonkey_con, jsonkey_def, jsonkey_rts, jsonkey_grpdata,
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

// remove leading slash from the given filename string (edit in-place)
static void AddonJson_RemoveLeadingSlash(char* filename)
{
    int i = 0, j = 0;
    while(filename[j] == '/') j++;
    if (j == 0) return;

    while(filename[j])
        filename[i++] = filename[j++];
    filename[i] = '\0';
}

// corrects the filepaths and checks if the specified file actually exists
static int32_t AddonJson_CorrectAndCheckFile(const useraddon_t * addonPtr, char* relpath, bool isgroup)
{
    if (!relpath || (relpath[0] == '\0'))
        return 0;

    Bcorrectfilename(relpath, 0);
    AddonJson_RemoveLeadingSlash(relpath);

    char fullpath[BMAX_PATH];
    // different path depending on package type
    if (addonPtr->package_type & (ADDONLT_GRP | ADDONLT_SSI | ADDONLT_ZIP))
        Bstrncpy(fullpath, relpath, BMAX_PATH);
    else if (addonPtr->package_type & (ADDONLT_FOLDER | ADDONLT_WORKSHOP))
        Bsnprintf(fullpath, BMAX_PATH, "%s/%s", addonPtr->data_path, relpath);
    else
    {
        LOG_F(ERROR, "Addon '%s' has invalid package type for filename check: %d!", addonPtr->internalId, addonPtr->package_type);
        return -1;
    }

    // try to open the path
    buildvfs_kfd jsonfil = kopen4load(fullpath, (isgroup) ? 2 : 0);
    if (jsonfil != buildvfs_kfd_invalid)
    {
        kclose(jsonfil);
        return 0;
    }

    LOG_F(ERROR, "File '%s' specified in addon '%s' does not exist!", fullpath, addonPtr->internalId);
    return 1;
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

// parse description -- may be sourced from an external file
static int32_t AddonJson_ParseDescription(useraddon_t *addonPtr, sjson_node *root, const char *key, bool isgroup)
{
    sjson_node * descNode = sjson_find_member_nocase(root, key);
    DO_FREE_AND_NULL(addonPtr->description);
    if (descNode == nullptr) return 1;

    if (descNode->tag == SJSON_STRING)
    {
        addonPtr->description = Xstrdup(descNode->string_);
        return 0;
    }
    else if (descNode->tag == SJSON_OBJECT)
    {
        sjson_node * fnode = sjson_find_member_nocase(descNode, jsonkey_scriptpath);

        const char *pathKey[] = { jsonkey_scriptpath };
        AddonJson_CheckUnknownKeys(addonPtr->internalId, fnode, key, pathKey, 1);

        if (!fnode || AddonJson_CheckStringTyped(addonPtr, fnode, key))
        {
            LOG_F(ERROR, "Addon description path missing or not a valid string for addon: %s", addonPtr->internalId);
            return -1;
        }

        char full_descpath[BMAX_PATH];
        char relative_descpath[BMAX_PATH];
        Bstrcpy(relative_descpath, fnode->string_);
        if (AddonJson_CorrectAndCheckFile(addonPtr, relative_descpath, isgroup))
            return -1;

        // different path depending on package type
        if (addonPtr->package_type & (ADDONLT_GRP | ADDONLT_SSI | ADDONLT_ZIP))
            Bstrncpy(full_descpath, relative_descpath, BMAX_PATH);
        else if (addonPtr->package_type & (ADDONLT_FOLDER | ADDONLT_WORKSHOP))
            Bsnprintf(full_descpath, BMAX_PATH, "%s/%s", addonPtr->data_path, relative_descpath);
        else
        {
            LOG_F(ERROR, "Unhandled package type %d in addon %s!", addonPtr->package_type, addonPtr->internalId);
            return -1;
        }

        buildvfs_kfd descfile = kopen4load(full_descpath, (isgroup ? 2 : 0));
        if (descfile == buildvfs_kfd_invalid)
            return -1;

        const int32_t len = kfilelength(descfile);
        char* descBuf = (char *)Xmalloc(len+1);
        descBuf[len] = '\0';
        if (kread_and_test(descfile, descBuf, len))
        {
            LOG_F(ERROR, "Failed to access and read description at '%s' for addon '%s'", full_descpath, addonPtr->internalId);
            kclose(descfile);
            Xfree(descBuf);
            return -1;
        }
        kclose(descfile);

        addonPtr->description = descBuf;
        return 0;
    }
    else
    {
        LOG_F(ERROR, "Invalid type in '%s' token for addon: %s", key, addonPtr->internalId);
        return -1;
    }
}

// parse arbitrary sjson string content and replace given reference pointer (nulled if key not found)
static int32_t AddonJson_ParseString(useraddon_t *addonPtr, sjson_node *root, const char *key, char* & dstPtr)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    DO_FREE_AND_NULL(dstPtr);

    if (ele == nullptr) return 1;
    else if (AddonJson_CheckStringTyped(addonPtr, ele, key)) return -1;

    dstPtr = Xstrdup(ele->string_);
    return 0;
}


// return 0 if given string satisfies the restrictions set on external identity
static int32_t AddonJson_CheckExternalIdentityRestrictions(const useraddon_t *addonPtr, const char* ident)
{
    if (!ident || !ident[0])
    {
        LOG_F(ERROR, "Identity string of addon '%s' cannot be empty!", addonPtr->internalId);
        return -1;
    }

    if (!isalpha(ident[0]))
    {
        LOG_F(ERROR, "Starting character in identity string of addon '%s' must be alphabetical!", addonPtr->internalId);
        return -1;
    }

    for (int i = 1; ident[i]; i++)
    {
        const char c = ident[i];
        if (isspace(c))
        {
            LOG_F(ERROR, "Identity string of addon '%s' may not contain whitespace!", addonPtr->internalId);
            return -1;
        }

        if (!isalnum(c) && c != '_' && c != '+' && c != '-')
        {
            LOG_F(ERROR, "Invalid character '%c' in identity string of addon '%s'!", c, addonPtr->internalId);
            LOG_F(INFO, "Valid characters are: { A-Z, a-z, 0-9, '_', '+', '-' }");
            return -1;
        }
    }

    return 0;
}

// get external identity used for dependency references, and check format
static int32_t AddonJson_ParseExternalId(useraddon_t *addonPtr, sjson_node *root, const char *key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    DO_FREE_AND_NULL(addonPtr->externalId);

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
    DO_FREE_AND_NULL(addonPtr->version);

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

// handle a single CON/DEF script json object
static int32_t AddonJson_HandleScriptObject(useraddon_t *addonPtr, sjson_node* snode, const char* key,
                                        char* & mscriptPtr, char** & modulebuffer, int32_t & nummodules)
{
    sjson_node *script_path, *script_type;
    AddonJson_CheckUnknownKeys(addonPtr->internalId, snode, key, json_scriptkeys, ARRAY_SIZE(json_scriptkeys));

    script_path = sjson_find_member_nocase(snode, jsonkey_scriptpath);
    if (script_path == nullptr || script_path->tag != SJSON_STRING)
    {
        LOG_F(ERROR, "Script path of key %s missing or has invalid format in addon '%s'!", key, addonPtr->internalId);
        return -1;
    }

    script_type = sjson_find_member_nocase(snode, jsonkey_scripttype);
    if (script_type == nullptr || script_type->tag != SJSON_STRING)
    {
        LOG_F(ERROR, "Script type of key %s missing or has invalid format in addon '%s'!", key, addonPtr->internalId);
        return -1;
    }

    if (!Bstrncasecmp(script_type->string_, jsonval_scriptmain, ARRAY_SIZE(jsonval_scriptmain)))
    {
        if (mscriptPtr)
        {
            LOG_F(ERROR, "More than one main '%s' script specified in addon '%s'!", key, addonPtr->internalId);
            return -1;
        }
        mscriptPtr = Xstrdup(script_path->string_);
        return 0;
    }
    else if (!Bstrncasecmp(script_type->string_, jsonval_scriptmodule, ARRAY_SIZE(jsonval_scriptmodule)))
    {
        modulebuffer[nummodules++] = Xstrdup(script_path->string_);
        return 0;
    }
    else
    {
        LOG_F(ERROR, "Invalid script type '%s' specified in addon '%s'!", script_type->string_, addonPtr->internalId);
        LOG_F(INFO, "Valid types are: {\"%s\", \"%s\"}", jsonval_scriptmain, jsonval_scriptmodule);
        return -1;
    }
}

// parse and store script file paths, check for file presence and other errors
static int32_t AddonJson_ParseScriptModules(useraddon_t *addonPtr, sjson_node* root, const char* key,
                                        char* & mscriptPtr, char** & modulebuffer, int32_t & modulecount)
{
    DO_FREE_AND_NULL(mscriptPtr);
    for (int i = 0; modulebuffer && i < modulecount; i++)
        Xfree(modulebuffer[i]);
    DO_FREE_AND_NULL(modulebuffer);
    modulecount = 0;

    bool hasError = false;
    int32_t numvalidmodules = 0;

    sjson_node * elem = sjson_find_member_nocase(root, key);
    if (elem == nullptr) return 1;

    if (elem->tag == SJSON_OBJECT)
    {
        // zero or one module
        modulebuffer = (char **) Xmalloc(1 * sizeof(char*));

        hasError = AddonJson_HandleScriptObject(addonPtr, elem, key, mscriptPtr, modulebuffer, numvalidmodules);
    }
    else if (elem->tag == SJSON_ARRAY)
    {
        // at most every object is a module
        modulebuffer = (char **) Xmalloc(sjson_child_count(elem) * sizeof(char*));

        sjson_node *snode;
        sjson_foreach(snode, elem)
        {
            if (snode->tag != SJSON_OBJECT)
            {
                LOG_F(ERROR, "Invalid type found in array of member '%s' of addon '%s'!", key, addonPtr->internalId);
                hasError = true;
                continue;
            }

            if (AddonJson_HandleScriptObject(addonPtr, snode, key, mscriptPtr, modulebuffer, numvalidmodules))
                hasError = true;
        }
    }
    else
    {
        LOG_F(ERROR, "Value of key '%s' of addon '%s' must be an array!", key, addonPtr->internalId);
        return -1;
    }

    // on error, abort and free valid items again
    if (hasError)
    {
        DO_FREE_AND_NULL(mscriptPtr);
        for (int i = 0; i < numvalidmodules; i++)
            Xfree(modulebuffer[i]);
        numvalidmodules = 0;
    }

    // valid children may be zero from error or no modules specified
    if (numvalidmodules == 0)
    {
        DO_FREE_AND_NULL(modulebuffer);
        modulecount = 0;
        return (hasError) ? -1 : 0;
    }
    else
    {
        modulebuffer = (char **) Xrealloc(modulebuffer, numvalidmodules * sizeof(char*));
        modulecount = numvalidmodules;
        return 0;
    }
}

static int32_t AddonJson_ParseGrpFilePaths(useraddon_t *addonPtr, sjson_node* root, const char* key)
{
    for (int i = 0; addonPtr->grp_datapaths && i < addonPtr->num_grp_datapaths; i++)
        Xfree(addonPtr->grp_datapaths[i]);
    DO_FREE_AND_NULL(addonPtr->grp_datapaths);
    addonPtr->num_grp_datapaths = 0;

    sjson_node * elem = sjson_find_member_nocase(root, key);
    if (elem == nullptr) return 1;

    bool hasError = false;
    int num_valid_grps = 0;
    if (elem->tag == SJSON_STRING)
    {
        addonPtr->grp_datapaths = (char **) Xmalloc(1 * sizeof(char*));
        addonPtr->grp_datapaths[num_valid_grps++] = Xstrdup(elem->string_);
    }
    else if (elem->tag == SJSON_ARRAY)
    {
        sjson_node *snode;
        addonPtr->grp_datapaths = (char **) Xmalloc(sjson_child_count(elem) * sizeof(char*));
        sjson_foreach(snode, elem)
        {
            if (AddonJson_CheckStringTyped(addonPtr, snode, key))
            {
                hasError = true;
                continue;
            }
            addonPtr->grp_datapaths[num_valid_grps++] = Xstrdup(snode->string_);
        }
    }
    else
    {
        LOG_F(ERROR, "Value of key '%s' of addon '%s' must be an array!", key, addonPtr->internalId);
        return -1;
    }

    if (hasError)
    {
        for (int i = 0; i < num_valid_grps; i++)
            Xfree(addonPtr->grp_datapaths[i]);
        DO_FREE_AND_NULL(addonPtr->grp_datapaths);
        return -1;
    }

    addonPtr->num_grp_datapaths = num_valid_grps;
    return 0;
}


// the version string in the dependency portion is prepended with comparison characters
static int32_t AddonJson_SetupDependencyVersion(addondependency_t * dep, const char* versionString)
{
    dep->cOp = AVCOMP_NOOP;
    DO_FREE_AND_NULL(dep->version);
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

static int32_t AddonJson_HandleDependencyObject(useraddon_t* addonPtr, sjson_node* snode, const char* key,
                                          addondependency_t*& dep_ptr, int32_t& num_valid_deps)
{
    sjson_node *dep_uid, *dep_version;
    AddonJson_CheckUnknownKeys(addonPtr->internalId, snode, key, json_dependencykeys, ARRAY_SIZE(json_dependencykeys));

    dep_uid = sjson_find_member_nocase(snode, jsonkey_depid);
    if (dep_uid == nullptr || dep_uid->tag != SJSON_STRING)
    {
        LOG_F(ERROR, "Dependency Id in key '%s' is missing or has invalid format in addon '%s'!", key, addonPtr->internalId);
        return -1;
    }

    dep_version = sjson_find_member_nocase(snode, jsonkey_version);
    if (dep_version != nullptr)
    {
        if (dep_version->tag != SJSON_STRING)
        {
            LOG_F(ERROR, "Dependency version %s in key '%s' is not a string in addon '%s'!", dep_uid->string_, key, addonPtr->internalId);
            return -1;
        }
    }

    addondependency_t & adt = dep_ptr[num_valid_deps];
    adt.setFulfilled(false);

    // required checks on dependency Id
    if (AddonJson_CheckExternalIdentityRestrictions(addonPtr, dep_uid->string_))
        return -1;

    // only bail if string specified and invalid, otherwise accept dependencies without version
    adt.dependencyId = Xstrdup(dep_uid->string_);
    if (dep_version && AddonJson_SetupDependencyVersion(&adt, dep_version->string_))
    {
        LOG_F(ERROR, "Invalid version string for dependency '%s' in addon: %s!", adt.dependencyId, addonPtr->internalId);
        DO_FREE_AND_NULL(adt.dependencyId);
        return -1;
    }

    num_valid_deps++;
    return 0;
}

// get the dependency property
static int32_t AddonJson_ParseDependencyList(useraddon_t* addonPtr, sjson_node* root, const char* key,
                                          addondependency_t*& dep_ptr, int32_t& num_valid_deps)
{
    DO_FREE_AND_NULL(dep_ptr);
    num_valid_deps = 0;

    int numchildren = 0;
    sjson_node * elem = sjson_find_member_nocase(root, key);
    if (elem == nullptr) return 1;

    if (elem->tag == SJSON_OBJECT)
    {
        numchildren = 1;
        dep_ptr = (addondependency_t *) Xcalloc(numchildren, sizeof(addondependency_t));
        AddonJson_HandleDependencyObject(addonPtr, elem, key, dep_ptr, num_valid_deps);
    }
    else if (elem->tag == SJSON_ARRAY)
    {
        numchildren = sjson_child_count(elem);
        dep_ptr = (addondependency_t *) Xcalloc(numchildren, sizeof(addondependency_t));

        sjson_node *snode;
        sjson_foreach(snode, elem)
        {
            if (snode->tag != SJSON_OBJECT)
            {
                LOG_F(ERROR, "Invalid type found in array of member '%s' of addon '%s'!", key, addonPtr->internalId);
                continue;
            }
            AddonJson_HandleDependencyObject(addonPtr, snode, key, dep_ptr, num_valid_deps);
        }
    }
    else
    {
        LOG_F(ERROR, "Content of member '%s' of addon '%s' is not an array!", key, addonPtr->internalId);
        return -1;
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
        return ADDONGF_ANY;

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

// add game crc -- assumes that the array is initialized
static int32_t AddonJson_AddGameCRC(useraddon_t* addonPtr, sjson_node* ele, const char* key, const int index)
{
    if (ele->tag == SJSON_NUMBER)
    {
        addonPtr->gamecrcs[index] = ele->number_;
        return 0;
    }
    else if (ele->tag == SJSON_STRING)
    {
        // hexadecimals aren't supported in json, hence have string option
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

        addonPtr->gamecrcs[index] = hex;
        return 0;
    }
    else
    {
        LOG_F(ERROR, "Invalid type for CRC on key '%s' for addon %s!", key, addonPtr->internalId);
        return -1;
    }
}

// The gameCRC acts as an additional method to finegrain control for which game the addon should show up.
static int32_t AddonJson_ParseGameCRC(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    DO_FREE_AND_NULL(addonPtr->gamecrcs);
    addonPtr->num_gamecrcs = 0;
    if (ele == nullptr) return 1;

    if (ele->tag == SJSON_ARRAY)
    {
        addonPtr->gamecrcs = (int32_t*) Xmalloc(sjson_child_count(ele) * sizeof(int32_t));

        sjson_node * snode;
        int crc_count = 0;

        sjson_foreach(snode, ele)
        {
            if (AddonJson_AddGameCRC(addonPtr, snode, key, crc_count++))
            {
                DO_FREE_AND_NULL(addonPtr->gamecrcs);
                return -1;
            }
        }
        addonPtr->num_gamecrcs = crc_count;
    }
    else
    {
        addonPtr->gamecrcs = (int32_t*) Xmalloc(sizeof(int32_t));
        if (AddonJson_AddGameCRC(addonPtr, ele, key, 0))
        {
            DO_FREE_AND_NULL(addonPtr->gamecrcs);
            return -1;
        }
        addonPtr->num_gamecrcs = 1;
    }

    return 0;
}

static int32_t AddonJson_ParseStartMap(useraddon_t* addonPtr, sjson_node* root, const char* key)
{
    sjson_node * ele = sjson_find_member_nocase(root, key);
    DO_FREE_AND_NULL(addonPtr->startmapfilename);
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

// check if files specified in addon struct exist
static int32_t AddonJson_CheckFilesPresence(const useraddon_t * addonPtr, bool isgroup)
{
    int missingCnt = 0;
    missingCnt += AddonJson_CorrectAndCheckFile(addonPtr, addonPtr->mscript_path, isgroup);
    missingCnt += AddonJson_CorrectAndCheckFile(addonPtr, addonPtr->mdef_path, isgroup);
    missingCnt += AddonJson_CorrectAndCheckFile(addonPtr, addonPtr->mrts_path, isgroup);
    missingCnt += AddonJson_CorrectAndCheckFile(addonPtr, addonPtr->preview_path, isgroup);
    missingCnt += AddonJson_CorrectAndCheckFile(addonPtr, addonPtr->startmapfilename, isgroup);

    for (int i = 0; i < addonPtr->num_con_modules; i++)
        missingCnt += AddonJson_CorrectAndCheckFile(addonPtr, addonPtr->con_modules[i], isgroup);

    for (int i = 0; i < addonPtr->num_def_modules; i++)
        missingCnt += AddonJson_CorrectAndCheckFile(addonPtr, addonPtr->def_modules[i], isgroup);

    for (int i = 0; i < addonPtr->num_grp_datapaths; i++)
        missingCnt += AddonJson_CorrectAndCheckFile(addonPtr, addonPtr->grp_datapaths[i], isgroup);

    return missingCnt;
}

// Load data from json file into addon -- assumes that unique ID for the addon has been defined!
static int32_t AddonJson_ParseDescriptor(sjson_context *ctx, char* json_fn, useraddon_t* addonPtr, const char* packfn)
{
    // open json descriptor (try 8.3 format as well, due to ken grp restrictions)
    const bool isgroup = addonPtr->package_type & (ADDONLT_ZIP | ADDONLT_GRP | ADDONLT_SSI);
    buildvfs_kfd jsonfil = kopen4load(json_fn, (isgroup ? 2 : 0));
    if (jsonfil == buildvfs_kfd_invalid)
    {
        json_fn[strlen(json_fn)-1] = '\0';
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
        LOG_F(ERROR, "Invalid game type specified for addon: '%s'! (key: %s)", addonPtr->internalId, jsonkey_game);
        jsonErrorCnt++;
    }

    // creator must specify an identity for the addon, such that other addons can reference it (required)
    parseResult = AddonJson_ParseExternalId(addonPtr, root, jsonkey_depid);
    if (parseResult != 0)
    {
        if (parseResult == 1) LOG_F(ERROR, "Missing identity for addon: '%s'! (key: %s)", addonPtr->internalId, jsonkey_depid);
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
    parseResult = AddonJson_ParseDescription(addonPtr, root, jsonkey_desc, isgroup);
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
    parseResult = AddonJson_ParseScriptModules(addonPtr, root, jsonkey_con, addonPtr->mscript_path,
                                                addonPtr->con_modules, addonPtr->num_con_modules);
    if (parseResult == -1) jsonErrorCnt++;

    // DEF script paths (optional)
    parseResult = AddonJson_ParseScriptModules(addonPtr, root, jsonkey_def, addonPtr->mdef_path,
                                                addonPtr->def_modules, addonPtr->num_def_modules);
    if (parseResult == -1) jsonErrorCnt++;

    // GRP Datapaths (optional)
    parseResult = AddonJson_ParseGrpFilePaths(addonPtr, root, jsonkey_grpdata);
    if (parseResult == -1) jsonErrorCnt++;

    // Preview image filepath (optional)
    parseResult = AddonJson_ParseString(addonPtr, root, jsonkey_image, addonPtr->preview_path);
    if (parseResult == -1) jsonErrorCnt++;

    // RTS file path (optional)
    parseResult = AddonJson_ParseString(addonPtr, root, jsonkey_rts, addonPtr->mrts_path);
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

    // after parsing all properties, check if filepaths exist
    parseResult = AddonJson_CheckFilesPresence(addonPtr, isgroup);
    if (parseResult > 0) jsonErrorCnt++;

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


#ifndef EDUKE32_RETAIL_MENU
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
            Bsnprintf(package_path, BMAX_PATH, "%s/%s", addondir, rec->name);

            // absolutely MUST be zero initialized
            useraddon_t* & addonPtr = s_useraddons[s_numuseraddons] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));

            // internal identity must be initialized first
            Bsnprintf(tempbuf, BMAX_PATH, "pkg/%s", rec->name);
            addonPtr->internalId = Xstrdup(tempbuf);

            // set data path and default loadorder index
            addonPtr->data_path = Xstrdup(package_path);
            Bcorrectfilename(addonPtr->data_path, 0);
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
            int parsingFailed = AddonJson_ParseDescriptor(ctx, json_path, addonPtr, rec->name);
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
#endif

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
        Bsnprintf(basepath, BMAX_PATH, "%s/%s", addondir, rec->name);

        // absolutely MUST be zero initialized
        useraddon_t* & addonPtr = s_useraddons[s_numuseraddons] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));

        // identity must be initialized first
        Bsnprintf(tempbuf, BMAX_PATH, "dir/%s", rec->name);
        addonPtr->internalId = Xstrdup(tempbuf);

        addonPtr->data_path = Xstrdup(basepath);
        Bcorrectfilename(addonPtr->data_path, 0);
        addonPtr->loadorder_idx = DEFAULT_LOADORDER_IDX;
        addonPtr->package_type = ADDONLT_FOLDER;

        char json_path[BMAX_PATH];
        Bsnprintf(json_path, BMAX_PATH, "%s/%s", basepath, addonjsonfn);
        if (AddonJson_ParseDescriptor(ctx, json_path, addonPtr, rec->name))
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
        AddonJson_ReadLocalSubfolders(ctx, &fnlist, addonpathbuf);
#ifndef EDUKE32_RETAIL_MENU
        AddonJson_ReadLocalPackages(ctx, &fnlist, addonpathbuf);
#endif
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
