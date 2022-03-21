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

#ifndef addongrpinfo_h_
#define addongrpinfo_h_

#include "addons.h"

#ifdef __cplusplus
extern "C" {
#endif

// addons loaded from .grpinfo files. Mutually exclusive and replace the selected GRP.
extern useraddon_t** g_useraddons_grpinfo;
extern int32_t g_addoncount_grpinfo;

// shorthands for common iteration types
#define for_grpaddons(_ptr, _body)\
    for (int _idx = 0; _idx < g_addoncount_grpinfo; _idx++)\
    {\
        useraddon_t* _ptr = g_useraddons_grpinfo[_idx];\
        _body;\
    }

void Addon_FreeGrpInfoAddons(void);
void Addon_ReadGrpInfoDescriptors(void);
int32_t Addon_LoadGrpInfoAddons(void);

#ifdef __cplusplus
}
#endif

#endif
