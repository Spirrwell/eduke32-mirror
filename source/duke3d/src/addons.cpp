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

// hack: this hashtable is (ab)used as a hash set
static hashtable_t h_addontemp = { MAXUSERADDONS, NULL };

// Cache for preview image data pointers, identified the addon path.
// Palette conversion is slow, hence we want to precache and store these images for later display
static hashtable_t h_addonpreviews = { MAXUSERADDONS, NULL };

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
static bool Addon_MatchesSelectedGame(const useraddon_t* addonPtr)
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

// extract first segment of version and the starting index of the next segment
// also returns the char after the segment ends
// e.g. given string "24.0.1" it will extract 24 and return '.'
static char Addon_ParseVersionSegment(const char* vString, int32_t & segment, int32_t & nextSegmentStartIndex)
{
    // this function assumes that strings were previously verified with AJ_CheckVersionFormat()
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
    // this function assumes that strings were previously verified with AJ_CheckVersionFormat()
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
        {
            if ((addonPtr->loadtype & LT_ZIP))
                popgroupfromkzstack();
            else
                popgroupfile();
        }

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

// iterate through all grp info addons, find selected one, change game grp
int32_t Addon_LoadGrpInfoAddons(void)
{
    if (g_addoncount_grpinfo <= 0 || !g_useraddons_grpinfo)
        return -1;

    for_grpaddons(addonPtr,
    {
        if (!addonPtr->isSelected() || !Addon_MatchesSelectedGame(addonPtr))
            continue;

        if (!addonPtr->isValid() || addonPtr->isTotalConversion() || !addonPtr->isGrpInfoAddon() || !addonPtr->grpfile)
        {
            LOG_F(ERROR, "Skip invalid grpinfo in init: %s. This shouldn't be happening.", addonPtr->internalId);
            continue;
        }

        g_selectedGrp = addonPtr->grpfile;
        break; // only load one at most
    });

    return 0;
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
