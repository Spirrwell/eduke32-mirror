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

#ifndef addons_h_
#define addons_h_

#ifdef __cplusplus
extern "C" {
#endif

#define MAXUSERADDONS 1024

// min and max character lengths for the strings stored in the addon structs
#define ADDON_MAXID 32
#define ADDON_MAXVERSION 32

#define ADDON_MAXTITLE 96
#define ADDON_MAXAUTHOR 128

// this isn't allocated for each struct, but is a maximum value for sanity reasons
#define ADDON_MAXDESC 16384

// preview images must adhere to these dimensions
#define PREVIEWTILE_XSIZE 320
#define PREVIEWTILE_YSIZE 200

#define ADDONFLAG_SELECTION (1 << 0)
#define ADDONFLAG_STARTMAP (1 << 1)

#define DEFAULT_LOADORDER_IDX (-1)

// the addon will only show up in the menu if these gameflags are met
enum addongame_t
{
    BASEGAME_NONE = 0,
    BASEGAME_ANY = GAMEFLAGMASK,
    BASEGAME_DUKE = GAMEFLAG_DUKE,
    BASEGAME_NAM = GAMEFLAG_NAM | GAMEFLAG_NAPALM,
    BASEGAME_WW2GI = GAMEFLAG_WW2GI,
    BASEGAME_FURY = GAMEFLAG_FURY,
};

enum
{
    ADDON_RENDNONE = 0,
    ADDON_RENDCLASSIC = (1 << 0),
    ADDON_RENDPOLYMOST = (1 << 1),
    ADDON_RENDPOLYMER = (1 << 2),
    ADDON_RENDMASK = (1 << 3) - 1,
};

// identifies the origin of the addon content (bitmap for simplified checks)
enum addonpackage_t
{
    LT_INVALID = 0,
    LT_ZIP = (1 << 0),      // ZIP, PK3, PK4
    LT_GRP = (1 << 1),      // KenS GRP
    LT_SSI = (1 << 2),      // Sunstorm
    LT_FOLDER = (1 << 3),   // Local Subfolder
    LT_WORKSHOP = (1 << 4), // Workshop Folder
    LT_GRPINFO = (1 << 5),  // internal and external grpinfo
};

enum vcomp_t
{
    VCOMP_NOOP = 0,
    VCOMP_EQ,
    VCOMP_GT,
    VCOMP_LT,
    VCOMP_GTEQ,
    VCOMP_LTEQ,
};

// reference to another addon
struct addondependency_t
{
    int8_t fulfilled;

    char depId[ADDON_MAXID];
    char version[ADDON_MAXVERSION];
    vcomp_t cOp;

    void setFulfilled(bool status) { fulfilled = (int) status; }
    bool isFulfilled() const { return (fulfilled != 0); }
};

// all data loaded from the json
struct addonjson_t
{
    // required for version and dependency checks. Not necessarily unique since user-determined.
    char externalId[ADDON_MAXID];
    char version[ADDON_MAXVERSION];

    // these are purely visual properties
    char title[ADDON_MAXTITLE];
    char author[ADDON_MAXAUTHOR];
    char* description = nullptr;

    // important file paths
    char preview_path[BMAX_PATH];
    char main_script_path[BMAX_PATH];
    char main_def_path[BMAX_PATH];
    char main_rts_path[BMAX_PATH];

    // modules, unlike the mainscript  files, can be loaded cumulatively
    char** script_modules = nullptr;
    char** def_modules = nullptr;
    int32_t num_script_modules = 0, num_def_modules = 0;

    // map to start and rendmode
    char boardfilename[BMAX_PATH];
    int32_t startlevel, startvolume;
    int32_t rendmode;

    // dependencies and incompatibilities
    addondependency_t* dependencies = nullptr;
    addondependency_t* incompatibles = nullptr;
    int32_t num_dependencies = 0, num_incompatibles = 0;
};

struct useraddon_t
{
    // unique id, automatically generated
    char* internalId;

    // this stores the text displayed on the menu entry, constructed below
    char menuentryname[ADDON_MAXTITLE + 16];

    // path that contains the addon's data
    char data_path[BMAX_PATH];

    // only used for internal or grpinfo addons
    const grpfile_t* grpfile;

    // base game for which the addon will show up, type of package, and json contents
    addongame_t gametype;
    int32_t gamecrc;

    addonpackage_t loadtype;
    addonjson_t jsondat;

    // pointer to binary image data
    uint8_t* image_data;

    uint32_t flags;
    int32_t loadorder_idx;
    int32_t mdeps, incompats;

    // getter and setter for selection status
    void setSelected(bool status)
    {
        if (status) flags |= ADDONFLAG_SELECTION;
        else flags &= ~ADDONFLAG_SELECTION;
    }
    bool isSelected() const
    {
        return (bool) (flags & ADDONFLAG_SELECTION);
    }

    bool isGrpInfoAddon() const { return loadtype == LT_GRPINFO; }
    bool isTotalConversion() const { return jsondat.main_script_path[0] || jsondat.main_def_path[0]; }
    bool isValid() const { return loadtype != LT_INVALID; }

    void updateMenuEntryName(int const startidx, int const maxVis)
    {
        // truncation is intentional
        int n = 0; char tempbuf[8]; int tempsize;
        menuentryname[0] = '\0';

        tempsize = Bsnprintf(tempbuf, 8, "(%c) ", isSelected() ? 'x': ' ');
        if (tempsize < 0) return;
        Bstrncat(menuentryname, tempbuf, maxVis - n);
        n += tempsize;

        if (loadorder_idx >= 0)
        {
            tempsize = Bsnprintf(tempbuf, 8, "%d: ", loadorder_idx + 1);
            if (tempsize < 0) return;
            Bstrncat(menuentryname, tempbuf, maxVis - n);
            n += tempsize;
        }

        Bstrncat(menuentryname, &jsondat.title[startidx], maxVis - n);
    }

    void countMissingDependencies(hashtable_t* h_temp = nullptr)
    {
        mdeps = 0;
        for (int i = 0; i < jsondat.num_dependencies; i++)
        {
            if (!jsondat.dependencies[i].isFulfilled())
            {
                if (h_temp) hash_add(h_temp, jsondat.dependencies[i].depId, (intptr_t) -1, true);
                mdeps++;
            }
        }
    }

    void countIncompatibleAddons(hashtable_t* h_temp = nullptr)
    {
        incompats = 0;
        for (int i = 0; i < jsondat.num_incompatibles; i++)
        {
            if (jsondat.incompatibles[i].isFulfilled())
            {
                if (h_temp) hash_add(h_temp, jsondat.incompatibles[i].depId, (intptr_t) -1, true);
                incompats++;
            }
        }
    }

};

// distinct types of addons, handled differently each
extern useraddon_t** g_useraddons_grpinfo;
extern useraddon_t** g_useraddons_tcs;
extern useraddon_t** g_useraddons_mods;

// counters for said addons
extern int32_t g_addoncount_grpinfo;
extern int32_t g_addoncount_tcs;
extern int32_t g_addoncount_mods;

#define TOTAL_ADDON_COUNT (g_addoncount_grpinfo + g_addoncount_tcs + g_addoncount_mods)

// global counters (selected, missing dependencies, incompatible addons)
extern int32_t g_num_selected_addons;
extern int32_t g_num_active_mdeps;
extern int32_t g_num_active_incompats;

extern int32_t g_addon_selrendmode;

extern bool g_addon_failedboot;
extern bool g_addon_strictdeps;

// preview image binary data is cached so expensive palette conversion does not need to be repeated
void Addon_FreePreviewHashTable(void);
void Addon_CachePreviewImages(void);
int32_t Addon_LoadPreviewTile(const useraddon_t* addon);

void Addon_FreeUserAddons(void);
void Addon_LoadDescriptors(void);
void Addon_PruneInvalidAddons(useraddon_t** & useraddons, int32_t & numuseraddons);

void Addon_InitializeLoadOrder(void);
void Addon_SwapLoadOrder(int32_t const indexA, int32_t const indexB, int32_t const maxvis);

bool Addon_GetStartMap(const char* & startfn, int32_t & startlevel, int32_t & startvolume);
void Addon_RefreshDependencyStates(void);

#ifdef USE_OPENGL
int32_t Addon_GetBootRendmode(void);
#endif

int32_t Addon_PrepareGrpInfoAddons(void);
int32_t Addon_PrepareUserTCs(void);
int32_t Addon_PrepareUserMods(void);

#ifdef __cplusplus
}
#endif

#endif
