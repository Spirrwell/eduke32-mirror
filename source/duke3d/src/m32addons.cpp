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

// This file implements addons.h for mapster32
// exists solely to allow calling Addon_PrepareUserAddons() from G_LoadGroups() in common.cpp
// Could later be expanded for editor-specific user addon support

#include "duke3d.h"
#include "addons.h"

useraddon_t** g_useraddons_grpinfo = nullptr;
useraddon_t** g_useraddons_tcs = nullptr;
useraddon_t** g_useraddons_mods = nullptr;

int32_t g_addoncount_grpinfo = 0;
int32_t g_addoncount_tcs = 0;
int32_t g_addoncount_mods = 0;

int32_t g_num_selected_addons = 0;
int32_t g_num_active_mdeps = 0;
int32_t g_num_active_incompats = 0;

uint32_t g_addon_compatrendmode = ADDON_RENDMASK;

bool g_addon_failedboot = false;

void Addon_FreePreviewHashTable(void){ /* stub */ };
void Addon_CachePreviewImages(void) { /* stub */ };
int32_t Addon_LoadPreviewTile(const useraddon_t* addon)
{
    // stub
    UNREFERENCED_PARAMETER(addon);
    return 0;
}

void Addon_FreeUserAddons(void) { /* stub */ };
void Addon_LoadDescriptors(void) { /* stub */ };
void Addon_PruneInvalidAddons(useraddon_t** & useraddons, int32_t & numuseraddons)
{
    UNREFERENCED_PARAMETER(useraddons);
    UNREFERENCED_PARAMETER(numuseraddons);
};

void Addon_InitializeLoadOrder(void) { /* stub */ };
void Addon_SwapLoadOrder(int32_t const indexA, int32_t const indexB, int32_t const maxvis)
{
    // stub
    (void) indexA;
    (void) indexB;
    (void) maxvis;
};

void Addon_RefreshDependencyStates(void) { /* stub */ };
int32_t Addon_CheckDependencyProblems(const useraddon_t* addonPtr)
{
    // stub
    (void) addonPtr;
    return 0;
};

bool Addon_GetStartMap(const char* & startfn, int32_t & startlevel, int32_t & startvolume)
{
    startlevel = 0;
    startvolume = 0;
    startfn = nullptr;
    return false;
}

#ifdef USE_OPENGL
int32_t Addon_GetBootRendmode(int32_t const rendmode)
{
    (void) rendmode;
    return -1;
};
#endif

int32_t Addon_PrepareGrpInfoAddons(void) { return 0; };
int32_t Addon_PrepareUserTCs(void) { return 0; };
int32_t Addon_PrepareUserMods(void) { return 0; };