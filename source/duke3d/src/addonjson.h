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

#ifndef addonjson_h_
#define addonjson_h_

#include "addons.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t AJ_CheckFilePresence(const char* filepath);
int32_t AJ_CheckVersionFormat(const char* versionString);
int32_t AJ_CompareVersionStrings(const char* versionA, const char* versionB);

int32_t AJ_ParseJsonDescriptor(char* json_fn, useraddon_t* addonPtr, const char* basepath, const char* packfn);


#ifdef __cplusplus
}
#endif

#endif
