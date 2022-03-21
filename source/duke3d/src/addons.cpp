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
#include "addonjson.h"
#include "addongrpinfo.h"

// hack: this hashtable is (ab)used as a hash set
static hashtable_t h_addontemp = { MAXUSERADDONS, NULL };

// Cache for preview image data pointers, identified the addon path.
// Palette conversion is slow, hence we want to precache and store these images for later display
static hashtable_t h_addonpreviews = { MAXUSERADDONS, NULL };

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

uint32_t g_addon_compatrendmode = ADDON_RENDMASK;
bool g_addon_failedboot = false;

int32_t g_num_selected_addons = 0;
int32_t g_num_active_mdeps = 0;
int32_t g_num_active_incompats = 0;

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
    for_tcaddons(addonPtr, { addonPtr->freeContents(); Xfree(addonPtr); });
    DO_FREE_AND_NULL(g_useraddons_tcs);
    g_addoncount_tcs = 0;

    for_modaddons(addonPtr, { addonPtr->freeContents(); Xfree(addonPtr); });
    DO_FREE_AND_NULL(g_useraddons_mods);
    g_addoncount_mods = 0;
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

// close recently opened package (removes most recently opened one)
static void Addon_PackageCleanup(const int32_t grpfileidx)
{
    if (grpfileidx < numgroupfiles)
        popgroupfile(); // remove grp/ssi
    else
        popgroupfromkzstack(); // remove zip
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
    int grpidx = 0, tcidx = 0, modidx = 0;
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

// Load preview contents from an image file and convert it to palette
static uint8_t* Addon_LoadPreviewFromFile(const char *fn)
{
    vec2_t xydim = {0, 0};
    uint8_t* imagebuffer = loadimagefromfile(fn, xydim);

    if (!imagebuffer)
    {
        LOG_F(ERROR, "Failed to load preview image: %s", fn);
        return nullptr;
    }

    if (xydim.x != PREVIEWTILE_XSIZE || xydim.y != PREVIEWTILE_YSIZE)
    {
        LOG_F(ERROR, "Addon preview image '%s' does not have required format: %dx%d", fn, PREVIEWTILE_XSIZE, PREVIEWTILE_YSIZE);
        return nullptr;
    }

    return imagebuffer;
}

// check if addon matches current game and crc, if specified
bool Addon_MatchesSelectedGame(const useraddon_t* addonPtr)
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

    int const result = AJ_CompareVersionStrings(packVersion, depVersion);
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

        for_grpaddons(otherAddon,
        {
            if (otherAddon->isSelected() && Addon_DependencyMatch(&dep, otherAddon))
            {
                dep.setFulfilled(true);
                break;
            }
        });

        if (dep.isFulfilled())
            continue;

        if (!addonPtr->isTotalConversion())
        {
            // TCs may depend on mods and grpinfo, but not other TCs
            for_tcaddons(otherAddon,
            {
                if (otherAddon->isSelected() && Addon_DependencyMatch(&dep, otherAddon))
                {
                    dep.setFulfilled(true);
                    break;
                }
            });

            if (dep.isFulfilled())
                continue;
        }

        // TODO: some mods may depend on load order, need some way to express this in the JSON
        for_modaddons(otherAddon,
        {
            if (otherAddon->isSelected() && Addon_DependencyMatch(&dep, otherAddon))
            {
                dep.setFulfilled(true);
                break;
            }
        });
    }
}

// update global counter for selected addons
static void Addon_UpdateCount_SelectedAddons(void)
{
    g_num_selected_addons = 0;
    for_grpaddons(addonPtr, g_num_selected_addons += addonPtr->isSelected());
    for_tcaddons(addonPtr, g_num_selected_addons += addonPtr->isSelected());
    for_modaddons(addonPtr, g_num_selected_addons += addonPtr->isSelected());
}

// update global counter for active missing dependencies
static void increment_global_mdepscounter(const char*, intptr_t) { g_num_active_mdeps++; } //hack
static void Addon_UpdateCount_MissingDependencies(void)
{
    hash_init(&h_addontemp);

    g_num_active_mdeps = 0;
    for_grpaddons(addonPtr, addonPtr->countMissingDependencies((addonPtr->isSelected()) ?  &h_addontemp : nullptr));
    for_tcaddons(addonPtr, addonPtr->countMissingDependencies((addonPtr->isSelected()) ?  &h_addontemp : nullptr));
    for_modaddons(addonPtr, addonPtr->countMissingDependencies((addonPtr->isSelected()) ?  &h_addontemp : nullptr));

    hash_loop(&h_addontemp, increment_global_mdepscounter);
    hash_free(&h_addontemp);
}

// update global counter for active incompatibilities
static void increment_global_incompatiblescounter(const char*, intptr_t) { g_num_active_incompats++; } //hack
static void Addon_UpdateCount_ActiveIncompatibles(void)
{
    hash_init(&h_addontemp);

    g_num_active_incompats = 0;
    for_grpaddons(addonPtr, addonPtr->countIncompatibleAddons((addonPtr->isSelected()) ?  &h_addontemp : nullptr));
    for_tcaddons(addonPtr, addonPtr->countIncompatibleAddons((addonPtr->isSelected()) ?  &h_addontemp : nullptr));
    for_modaddons(addonPtr, addonPtr->countIncompatibleAddons((addonPtr->isSelected()) ?  &h_addontemp : nullptr));

    hash_loop(&h_addontemp, increment_global_incompatiblescounter);
    hash_free(&h_addontemp);
}

static void Addon_UpdateSelectedRendmode(void)
{
    g_addon_compatrendmode = ADDON_RENDMASK;
    for_grpaddons(addonPtr, if (addonPtr->isSelected()) g_addon_compatrendmode &= addonPtr->jsondat.compat_rendmodes);
    for_tcaddons(addonPtr, if (addonPtr->isSelected()) g_addon_compatrendmode &= addonPtr->jsondat.compat_rendmodes);
    for_modaddons(addonPtr, if (addonPtr->isSelected()) g_addon_compatrendmode &= addonPtr->jsondat.compat_rendmodes);
}

// Important: this function is called before the setup window is shown
// Hence it must not depend on any variables initialized from game content
void Addon_ReadJsonDescriptors(void)
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
        if (addonPtr->isValid() && Addon_MatchesSelectedGame(addonPtr))
            newaddoncount++;
    }

    useraddon_t** gooduseraddons = (useraddon_t **) Xcalloc(newaddoncount, sizeof(useraddon_t*));

    for (i=0, j=0; i < numuseraddons; i++)
    {
        useraddon_t* addonPtr = useraddons[i];
        if (addonPtr->isValid()  && Addon_MatchesSelectedGame(addonPtr))
            gooduseraddons[j++] = addonPtr;
        else
        {
            addonPtr->freeContents();
            Xfree(addonPtr);
        }
    }
    Xfree(useraddons);

    useraddons = gooduseraddons;
    numuseraddons = newaddoncount;
}

static void Addon_LoadAddonPreview(useraddon_t* addonPtr)
{
    // don't cache images for addons we won't see
    if (!(addonPtr->isValid() && addonPtr->jsondat.preview_path[0] && Addon_MatchesSelectedGame(addonPtr)))
        return;

    intptr_t cachedImage = hash_find(&h_addonpreviews, addonPtr->jsondat.preview_path);
    if (cachedImage != -1)
        addonPtr->image_data = (uint8_t*) cachedImage;
    else
    {
        if (addonPtr->loadtype & (LT_GRP | LT_ZIP | LT_SSI))
            initgroupfile(addonPtr->data_path);

        addonPtr->image_data = Addon_LoadPreviewFromFile(addonPtr->jsondat.preview_path);

        if (addonPtr->loadtype & (LT_GRP | LT_ZIP | LT_SSI))
            Addon_PackageCleanup((addonPtr->loadtype & LT_ZIP) ? numgroupfiles : 0);

        // even store nullpointers, indicates that we shouldn't try again
        hash_add(&h_addonpreviews, addonPtr->jsondat.preview_path, (intptr_t) addonPtr->image_data, 0);
    }
}

// initializing of preview images requires access to palette, and is run after game content is loaded
void Addon_LoadPreviewImages(void)
{
    if ((G_GetLogoFlags() & LOGO_NOADDONS))
        return;

    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;
    for_grpaddons(addonPtr, Addon_LoadAddonPreview(addonPtr));
    for_tcaddons(addonPtr, Addon_LoadAddonPreview(addonPtr));
    for_modaddons(addonPtr, Addon_LoadAddonPreview(addonPtr));
    pathsearchmode = bakpathsearchmode;
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

static int32_t Addon_InitLoadOrderFromConfig(useraddon_t** addonlist, int32_t const numaddons)
{
    int32_t maxLoadOrder = 0;
    for (int i = 0; i < numaddons; i++)
    {
        useraddon_t* addonPtr = addonlist[i];
        int32_t k = CONFIG_GetAddonLoadOrder(addonPtr->internalId);
        addonPtr->loadorder_idx = (k >= 0) ? k : 0;
        maxLoadOrder = (k > maxLoadOrder) ? k : maxLoadOrder;
    }
    return maxLoadOrder + 1;
}

static void Addon_InitAndSanitizeLoadOrder(useraddon_t** addonlist, int32_t const numaddons)
{
    if (numaddons <= 0 || !addonlist)
        return;

    int32_t i, cl, maxBufSize;
    int16_t maxLoadOrder = Addon_InitLoadOrderFromConfig(addonlist, numaddons);

    // allocate enough space for the case where all load order indices are duplicates
    maxBufSize = maxLoadOrder + numaddons;
    useraddon_t** lobuf = (useraddon_t**) Xcalloc(maxBufSize, sizeof(useraddon_t*));

    for (int i = 0; i < numaddons; i++)
    {
        useraddon_t* addonPtr = addonlist[i];
        cl = addonPtr->loadorder_idx;
        if (cl < 0 || lobuf[cl]) lobuf[maxLoadOrder++] = addonPtr;
        else lobuf[cl] = addonPtr;
    }

    // clean up load order
    int16_t newlo = 0;
    for (i = 0; i < maxLoadOrder; i++)
    {
        if (lobuf[i])
        {
            lobuf[i]->loadorder_idx = newlo++;
            CONFIG_SetAddonActivationStatus(lobuf[i]->internalId, lobuf[i]->isSelected());
            CONFIG_SetAddonLoadOrder(lobuf[i]->internalId, lobuf[i]->loadorder_idx);
        }
    }
    Xfree(lobuf);
}

// initialize load order for mods, and sanitize it so there are no gaps or duplicates
void Addon_InitializeLoadOrders(void)
{
    Addon_InitAndSanitizeLoadOrder(g_useraddons_tcs, g_addoncount_tcs);
    Addon_InitAndSanitizeLoadOrder(g_useraddons_mods, g_addoncount_mods);
}

// update dependency states of all addons, based on currently selected addons
void Addon_RefreshPropertyTrackers(void)
{
    for_tcaddons(addonPtr,{
        Addon_UpdateDependencies(addonPtr, addonPtr->jsondat.dependencies, addonPtr->jsondat.num_dependencies);
        Addon_UpdateDependencies(addonPtr, addonPtr->jsondat.incompatibles, addonPtr->jsondat.num_incompatibles);
    });

    for_modaddons(addonPtr,{
        Addon_UpdateDependencies(addonPtr, addonPtr->jsondat.dependencies, addonPtr->jsondat.num_dependencies);
        Addon_UpdateDependencies(addonPtr, addonPtr->jsondat.incompatibles, addonPtr->jsondat.num_incompatibles);
    });

    Addon_UpdateCount_SelectedAddons();
    Addon_UpdateCount_MissingDependencies();
    Addon_UpdateCount_ActiveIncompatibles();
    Addon_UpdateSelectedRendmode();

    // DLOG_F(INFO, "Number of selected addons: %d", g_num_selected_addons);
    // DLOG_F(INFO, "Number of missing dependencies of selected addons: %d", g_num_active_mdeps);
    // DLOG_F(INFO, "Number of addons that are incompatible with selected addons: %d", g_num_active_incompats);
}


const char* Addon_RetrieveStartMap(int32_t & startlevel, int32_t & startvolume)
{
    // assume that load order already sanitized, each index unique
    useraddon_t** lobuf = (useraddon_t**) Xcalloc(g_addoncount_mods, sizeof(useraddon_t*));
    for_modaddons(addonPtr, lobuf[addonPtr->loadorder_idx] = addonPtr);

    // addons in reverse load order
    for (int i = g_addoncount_mods-1; i >= 0; i--)
    {
        useraddon_t* addonPtr = lobuf[i];
        if (!(addonPtr->isValid() && addonPtr->isSelected() && Addon_MatchesSelectedGame(addonPtr)))
            continue;

        if (addonPtr->flags & ADDONFLAG_STARTMAP)
        {
            xfree(lobuf);
            startlevel = addonPtr->jsondat.startlevel;
            startvolume = addonPtr->jsondat.startvolume;
            return addonPtr->jsondat.boardfilename;

        }
    }

    // also go through TCs in reverse order
    lobuf = (useraddon_t**) Xrealloc(lobuf, g_addoncount_tcs * sizeof(useraddon_t*));
    for_tcaddons(addonPtr, lobuf[addonPtr->loadorder_idx] = addonPtr);
    for (int i = g_addoncount_tcs-1; i >= 0; i--)
    {
        useraddon_t* addonPtr = lobuf[i];
        if (!(addonPtr->isValid() && addonPtr->isSelected() && Addon_MatchesSelectedGame(addonPtr)))
            continue;

        if (addonPtr->flags & ADDONFLAG_STARTMAP)
        {
            xfree(lobuf);
            startlevel = addonPtr->jsondat.startlevel;
            startvolume = addonPtr->jsondat.startvolume;
            return addonPtr->jsondat.boardfilename;
        }
    }
    xfree(lobuf);

    startlevel = -1;
    startvolume = -1;
    return nullptr;
}

#ifdef USE_OPENGL
int32_t Addon_GetBootRendmode(int32_t const rendmode)
{
    // change current rendmode if it is incompatible
    if (!(g_bootState & BOOTSTATE_ADDONS))
        return -1;

    uint32_t tr_rendmode;
    switch (rendmode)
    {
        case REND_CLASSIC:
            tr_rendmode = ADDON_RENDCLASSIC;
            break;
        case REND_POLYMOST:
            tr_rendmode = ADDON_RENDPOLYMOST;
            break;
#ifdef POLYMER
        case REND_POLYMER:
            tr_rendmode = ADDON_RENDPOLYMER;
            break;
#endif
        default:
            tr_rendmode = ADDON_RENDNONE;
            break;
    }

    if ((tr_rendmode & g_addon_compatrendmode) == 0)
    {
#ifdef POLYMER
        if (g_addon_compatrendmode & ADDON_RENDPOLYMER) return REND_POLYMER;
        else
#endif
        {
            if (g_addon_compatrendmode & ADDON_RENDPOLYMOST) return REND_POLYMOST;
            else if (g_addon_compatrendmode & ADDON_RENDCLASSIC) return REND_CLASSIC;
        }
    }
    return -1;
}
#endif


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

// iterate through all tcs, find selected one, initialize data
int32_t Addon_LoadUserTCs(void)
{
    if (g_addoncount_tcs <= 0 || !g_useraddons_tcs)
        return -1;

    // use absolute paths to load addons
    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    // assume that load order already sanitized, each index unique
    useraddon_t** lobuf = (useraddon_t**) Xcalloc(g_addoncount_tcs, sizeof(useraddon_t*));
    for_tcaddons(addonPtr, lobuf[addonPtr->loadorder_idx] = addonPtr);

    for (int i = 0; i < g_addoncount_tcs; i++)
    {
        useraddon_t* addonPtr = lobuf[i];
        if (!addonPtr->isSelected() || !Addon_MatchesSelectedGame(addonPtr))
            continue;

        // sanity checks
        if (!addonPtr->isValid() || !addonPtr->isTotalConversion() || addonPtr->isGrpInfoAddon())
        {
            LOG_F(ERROR, "Skip invalid addon in TC init: %s. This shouldn't be happening.", addonPtr->internalId);
            continue;
        }

        Addon_PrepareUserAddon(addonPtr);
    }

    pathsearchmode = bakpathsearchmode;
    Xfree(lobuf);

    return 0;
}

// iterate through all mods in load order, find selected ones, initialize data
int32_t Addon_LoadUserMods(void)
{
    if (g_addoncount_mods <= 0 || !g_useraddons_mods)
        return -1;

    // use absolute paths to load addons
    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    // assume that load order already sanitized, each index unique
    useraddon_t** lobuf = (useraddon_t**) Xcalloc(g_addoncount_mods, sizeof(useraddon_t*));
    for_modaddons(addonPtr, lobuf[addonPtr->loadorder_idx] = addonPtr);

    // addons in load order
    for (int i = 0; i < g_addoncount_mods; i++)
    {
        useraddon_t* addonPtr = lobuf[i];
        if (!addonPtr->isSelected() || !Addon_MatchesSelectedGame(addonPtr))
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
