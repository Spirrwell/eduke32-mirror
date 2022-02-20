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

#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAXADDONTITLE 128
#define MAXADDONAUTHOR 128
#define MAXADDONVERSION 32
#define MAXADDONDESC 32768

#define PREVIEWTILE_XSIZE 320
#define PREVIEWTILE_YSIZE 200

enum addongame_t
{
    BASEGAME_ANY = GAMEFLAGMASK,
    BASEGAME_DUKE = GAMEFLAG_DUKE,
    BASEGAME_NAM = GAMEFLAG_NAM | GAMEFLAG_NAPALM,
    BASEGAME_WW2GI = GAMEFLAG_WW2GI,
    BASEGAME_NAM_WW2GI = GAMEFLAG_NAM | GAMEFLAG_NAPALM | GAMEFLAG_WW2GI,
    BASEGAME_FURY = GAMEFLAG_FURY,
};

enum addonpackage_t
{
    LT_INVALID = 0,
    LT_ZIP,      // ZIP, PK3, PK4
    LT_GRP,      // KenS GRP
    LT_SSI,      // Sunstorm
    LT_FOLDER,   // Local Subfolder
    LT_WORKSHOP, // Workshop Folder
};

struct addonjson_t
{
    char title[MAXADDONTITLE];
    char author[MAXADDONAUTHOR];
    char version[MAXADDONVERSION];

    char main_script_path[BMAX_PATH];
    char main_def_path[BMAX_PATH];
    char main_rts_path[BMAX_PATH];

    uint32_t flags;
    int32_t desc_len, desc_linecnt;
    int32_t num_script_modules, num_def_modules;

    char* description;
    uint8_t* image_data;

    char** script_modules;
    char** def_modules;
};

struct useraddon_t
{
    char* uniqueId;
    char menuentryname[MAXADDONTITLE];
    char data_path[BMAX_PATH];

    addonpackage_t loadtype;
    addonjson_t jsondat;

    int16_t loadorder_idx;
    uint8_t selected;

    void updateMenuEntryName()
    {
        Bsnprintf(menuentryname, MAXADDONTITLE, "%d: %s", loadorder_idx + 1, jsondat.title);
    }

    bool isValid()
    {
        return loadtype != LT_INVALID;
    }
};

extern useraddon_t * g_useraddons;
extern uint16_t g_numuseraddons;

void Addon_FreePreviewHashTable(void);
void Addon_FreeUserAddons(void);
int32_t Addon_ReadPackageDescriptors(void);
int32_t Addon_LoadPreviewTile(addonjson_t* mjsonStore);
void Addon_SwapLoadOrder(int32_t indexA, int32_t indexB);

int32_t Addon_PrepareSelectedAddon(useraddon_t* addon);
int32_t Addon_StartSelectedAddons(void);

#ifdef __cplusplus
}
#endif

#endif