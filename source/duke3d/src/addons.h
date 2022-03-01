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

#define ADDON_MINUID 8
#define ADDON_MAXUID 64

#define ADDON_MAXTITLE 128
#define ADDON_MAXAUTHOR 128
#define ADDON_MAXVERSION 32
#define ADDON_MAXDESC 32768

#define PREVIEWTILE_XSIZE 320
#define PREVIEWTILE_YSIZE 200

#define DEFAULT_LOADORDER_IDX (-1)

extern int32_t m_addondesc_lblength;
extern int32_t m_addontitle_maxvisible;

enum addongame_t
{
    BASEGAME_NONE = 0,
    BASEGAME_ANY = GAMEFLAGMASK,
    BASEGAME_DUKE = GAMEFLAG_DUKE,
    BASEGAME_NAM = GAMEFLAG_NAM | GAMEFLAG_NAPALM,
    BASEGAME_WW2GI = GAMEFLAG_WW2GI,
    BASEGAME_FURY = GAMEFLAG_FURY,
};

enum addonpackage_t
{
    LT_INVALID = 0,
    LT_ZIP = (1 << 0),      // ZIP, PK3, PK4
    LT_GRP = (1 << 1),      // KenS GRP
    LT_SSI = (1 << 2),      // Sunstorm
    LT_FOLDER = (1 << 3),   // Local Subfolder
    LT_WORKSHOP = (1 << 4), // Workshop Folder
    LT_INTERNAL = (1 << 5), // For Official Addons
};

enum addonflags_t
{
    ADDFLAG_SELECTED = 1,
    ADDFLAG_GRPFILE = 2,
};

enum version_comparator_t
{
    ADDV_NOOP = 0,
    ADDV_EQ,
    ADDV_GT_EQ,
    ADDV_LT_EQ,
    ADDV_GT,
    ADDV_LT,
};

struct addondependency_t
{
    char uniqueId[ADDON_MAXUID];
    char version[ADDON_MAXVERSION];
    char title[ADDON_MAXTITLE];

    bool fulfilled;
    version_comparator_t comparisonOp;
};

struct addonjson_t
{
    char title[ADDON_MAXTITLE];
    char author[ADDON_MAXAUTHOR];
    char version[ADDON_MAXVERSION];

    char* description;
    int32_t desc_linecnt;

    char preview_path[BMAX_PATH];
    char main_script_path[BMAX_PATH];
    char main_def_path[BMAX_PATH];
    char main_rts_path[BMAX_PATH];

    char** script_modules;
    char** def_modules;
    int32_t num_script_modules, num_def_modules;
};

struct useraddon_t
{
    char uniqueId[ADDON_MAXUID];
    char menuentryname[ADDON_MAXTITLE];
    char data_path[BMAX_PATH];

    // only used for official addons
    grpfile_t * grpfile;

    addongame_t gametype;
    addonpackage_t loadtype;
    addonjson_t jsondat;

    addondependency_t* dependencies;
    addondependency_t* incompatibilities;
    int32_t num_dependencies, num_incompats;

    uint8_t* image_data;

    uint16_t flags;
    int16_t loadorder_idx;

    bool isSelected()
    {
        return (flags & 1) != 0;
    }

    bool isGrpInfoAddon()
    {
        return (flags & 2) != 0;
    }

    bool isTotalConversion()
    {
        return jsondat.main_script_path[0] || jsondat.main_def_path[0];
    }

    bool isValid()
    {
        return loadtype != LT_INVALID;
    }

    void updateMenuEntryName(int const titleIdx = 0)
    {
        if (isGrpInfoAddon() || isTotalConversion())
            Bstrncpyz(menuentryname, &jsondat.title[titleIdx], m_addontitle_maxvisible);
        else
            Bsnprintf(menuentryname, m_addontitle_maxvisible, "%d: %s", loadorder_idx + 1, &jsondat.title[titleIdx]);
    }

};

extern useraddon_t* g_useraddons_grpinfo;
extern useraddon_t* g_useraddons_tcs;
extern useraddon_t* g_useraddons_mods;

extern int32_t g_addoncount_grpinfo;
extern int32_t g_addoncount_tcs;
extern int32_t g_addoncount_mods;

#define TOTAL_ADDON_COUNT (g_addoncount_grpinfo + g_addoncount_tcs + g_addoncount_mods)

extern bool g_addonstart_failed;

void Addon_FreePreviewHashTable(void);
void Addon_CachePreviewImages(void);
int32_t Addon_LoadPreviewTile(useraddon_t* addon);

void Addon_FreeUserAddons(void);
void Addon_ReadPackageDescriptors(void);
void Addon_PruneInvalidAddons(void);

void Addon_InitializeLoadOrder(void);
void Addon_SwapLoadOrder(int32_t const indexA, int32_t const indexB);

int32_t Addon_CheckDependencyFulfilled(addondependency_t* dep, useraddon_t* otherAddon);

int32_t Addon_PrepareGrpInfoAddon(void);
int32_t Addon_PrepareUserTCs(void);
int32_t Addon_PrepareUserMods(void);

#ifdef __cplusplus
}
#endif

#endif
