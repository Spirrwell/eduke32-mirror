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

#include "vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAXLEN_ADDONTITLE 32
#define MAXLEN_ADDONAUTHOR 32
#define MAXLEN_ADDONVERSION 16
#define MAXLEN_ADDONDATE 32
#define MAXLEN_ADDONDESC 8192

#define PREVIEWTILEX 320
#define PREVIEWTILEY 200

enum addontype_t
{
    ATYPE_INVALID = -1,
    ATYPE_MAIN = 0,
    ATYPE_MODULE = 1,
};

// internal struct
struct addonjson_t
{
    addonjson_t()
    {
        reset();
    }

    // data path
    char dataPath[BMAX_PATH];

    // preview image
    char imageBuffer[PREVIEWTILEX * PREVIEWTILEY];
    bool invalidImage;

    // type and dependency
    addontype_t addonType;
    uint32_t dependencyCRC;

    // visual descriptors
    char title[MAXLEN_ADDONTITLE];
    char author[MAXLEN_ADDONAUTHOR];
    char version[MAXLEN_ADDONVERSION];
    char description[MAXLEN_ADDONDESC];
    int32_t desclinecount;

    // main script paths
    char scriptNamePath[BMAX_PATH];
    char defNamePath[BMAX_PATH];
    char rtsNamePath[BMAX_PATH];

    // modules
    int32_t numCONModules;
    char** scriptModules;

    int32_t numDEFModules;
    char** defModules;

    void reset()
    {
        addonType = ATYPE_INVALID;
        dataPath[0] = '\0';

        imageBuffer[0] = '\0';
        invalidImage = false;

        title[0] = '\0';
        author[0] = '\0';
        version[0] = '\0';
        description[0] = '\0';
        desclinecount = 0;

        scriptNamePath[0] = '\0';
        defNamePath[0] = '\0';
        rtsNamePath[0] = '\0';

        if (!scriptModules)
        {
            for (int i = 0; i < numCONModules; i++)
            {
                Xfree(scriptModules[i]);
            }
            Xfree(scriptModules);
        }
        scriptModules = nullptr;
        numCONModules = 0;

        if (!defModules)
        {
            for (int i = 0; i < numDEFModules; i++)
            {
                Xfree(defModules[i]);
            }
            Xfree(defModules);
        }
        defModules = nullptr;
        numDEFModules = 0;
    }

    bool isValid() const
    {
        return addonType != ATYPE_INVALID;
    }
};

enum aloadtype_t
{
    LT_INVALID = -1,
    LT_FOLDER = 0, // Local Subfolder
    LT_ZIP = 1, // ZIP, PK3, PK4, GRP as ZIP etc.
    LT_GRP = 2, // Ken GRP
    LT_SSI = 3, // Sunstorm
    LT_WORKSHOP = 4 // Workshop Folder
};

struct menuaddon_t
{
    char entryname[MAXLEN_ADDONTITLE + 8];

    int16_t loadOrderIndex;
    bool selected;

    aloadtype_t loadType;
    addonjson_t jsonDat;


    bool isValid()
    {
        return loadType != LT_INVALID && jsonDat.isValid();
    }

    void clear()
    {
        loadType = LT_INVALID;
        loadOrderIndex = -1;
        jsonDat.reset();
        selected = false;
    }
};

extern menuaddon_t * g_menuaddons;
extern uint16_t g_nummenuaddons;

int32_t ReadAddonPackageDescriptors(void);
int32_t LoadAddonPreviewImage(addonjson_t* mjsonStore);
void SwapLoadOrder(int32_t indexA, int32_t indexB);
void CleanUpLoadOrder(void);
int32_t PrepareSelectedAddon(menuaddon_t* addon);
int32_t StartSelectedAddons(void);

#ifdef __cplusplus
}
#endif

#endif