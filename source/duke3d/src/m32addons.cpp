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

useraddon_t * g_useraddons = nullptr;
int32_t g_numuseraddons = 0;
bool g_addonfailed = false;

void Addon_FreeUserAddons(void){ /* stub */ };
void Addon_FreePreviewHashTable(void){ /* stub */ };

void Addon_SwapLoadOrder(int32_t const indexA, int32_t const indexB)
{
    // stub
    (void) indexA;
    (void) indexB;
}

int32_t Addon_ReadPackageDescriptors(void)
{
    // stub
    return 0;
}

void Addon_InitializeLoadOrder(void)
{
    //stub
}

int32_t Addon_PruneInvalidAddons(void)
{
    // stub
    return 0;
}

int32_t Addon_CachePreviewImages(void)
{
    // stub
    return 0;
}

int32_t Addon_LoadPreviewTile(useraddon_t* addon)
{
    // stub
    UNREFERENCED_PARAMETER(addon);
    return 0;
}

int32_t Addon_LoadSelectedGrpFileAddon(void)
{
    //stub
    return 0;
}

int32_t Addon_PrepareUserAddons(void)
{
    // stub
    return 0;
};
