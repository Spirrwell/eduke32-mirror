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

#define ADDON_MAXTITLE 128
#define ADDON_MAXAUTHOR 128
#define ADDON_MAXVERSION 32
#define ADDON_MAXDESC 32768

#define PREVIEWTILE_XSIZE 320
#define PREVIEWTILE_YSIZE 200

extern int32_t m_menudesc_lblength;
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

struct addonjson_t
{
    char title[ADDON_MAXTITLE];
    char author[ADDON_MAXAUTHOR];
    char version[ADDON_MAXVERSION];

    char* description;
    int32_t desc_len, desc_linecnt;

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
    char* uniqueId;
    char menuentryname[ADDON_MAXTITLE];
    char data_path[BMAX_PATH];

    // only used for official addons
    grpfile_t * grpfile;

    addongame_t gametype;
    addonpackage_t loadtype;
    addonjson_t jsondat;

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
        char pbuf[8];
        if (isGrpInfoAddon())
            Bstrcpy(pbuf, "GRP");
        else if (isTotalConversion())
            Bstrcpy(pbuf, "TC");
        else
            Bsnprintf(pbuf, 8, "%d", loadorder_idx + 1);

        Bsnprintf(menuentryname, m_addontitle_maxvisible, "%s: %s", pbuf, &jsondat.title[titleIdx]);
    }

};

extern useraddon_t * g_useraddons;
extern int32_t g_numuseraddons;
extern bool g_addonfailed;

void Addon_FreePreviewHashTable(void);
void Addon_FreeUserAddons(void);
void Addon_InitializeLoadOrder(void);
void Addon_SwapLoadOrder(int32_t const indexA, int32_t const indexB);

int32_t Addon_ReadPackageDescriptors(void);
int32_t Addon_PruneInvalidAddons(void);
int32_t Addon_CachePreviewImages(void);
int32_t Addon_LoadPreviewTile(useraddon_t* addon);
int32_t Addon_LoadSelectedGrpInfoAddon(void);
int32_t Addon_PrepareUserAddons(void);

#ifdef __cplusplus
}
#endif

#endif
