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

#ifdef ADDONS_MENU

#include "addongrpinfo.h"
#include "addonjson.h"

#ifdef __cplusplus
extern "C" {
#endif

// used for the hash tables
#define MAXUSERADDONS 1024
#define DEFAULT_LOADORDER_IDX (-1)

// menu entry name properties
#define ADDON_MAXENTRYNAME 64
#define ADDON_VISENTRYNAME 44

// preview images must adhere to these dimensions
#define PREVIEWTILE_XSIZE 320
#define PREVIEWTILE_YSIZE 200

enum // addongameflag_t
{
    ADDONGF_NONE = 0,
    ADDONGF_ANY = GAMEFLAGMASK,
    ADDONGF_DUKE = GAMEFLAG_DUKE,
    ADDONGF_NAM = GAMEFLAG_NAM | GAMEFLAG_NAPALM,
    ADDONGF_WW2GI = GAMEFLAG_WW2GI,
    ADDONGF_FURY = GAMEFLAG_FURY,
};

enum // addonrendmode_t
{
    ADDONRM_NONE = 0,
    ADDONRM_CLASSIC = (1 << 0),
    ADDONRM_POLYMOST = (1 << 1),
    ADDONRM_POLYMER = (1 << 2),
    ADDONRM_MASK = (1 << 3) - 1,
};

enum // addontype_t
{
    ADDONTYPE_INVALID = 0,
    ADDONTYPE_GRPINFO = (1 << 0),
    ADDONTYPE_TC = (1 << 1),
    ADDONTYPE_MOD = (1 << 2),
};

enum // addonpackage_t
{
    ADDONLT_INVALID = 0,
    ADDONLT_ZIP = (1 << 0),      // ZIP, PK3, PK4
    ADDONLT_GRP = (1 << 1),      // KenS GRP
    ADDONLT_SSI = (1 << 2),      // Sunstorm
    ADDONLT_FOLDER = (1 << 3),   // Local Subfolder
    ADDONLT_WORKSHOP = (1 << 4), // Workshop Folder
    ADDONLT_GRPINFO = (1 << 5),  // internal and external grpinfo
};

enum // addonvcomp_t
{
    AVCOMP_NOOP = 0,
    AVCOMP_EQ,
    AVCOMP_GT,
    AVCOMP_LT,
    AVCOMP_GTEQ,
    AVCOMP_LTEQ,
};

enum // addonflag_t
{
    ADDONFLAG_NONE = 0,
    ADDONFLAG_SELECTED = (1 << 0),
    ADDONFLAG_STARTMAP = (1 << 1),
    ADDONFLAG_OFFICIAL = (1 << 2),
};


#if defined(POLYMER) && defined(USE_OPENGL)
#define ADDON_SUPPORTED_RENDMODES (ADDONRM_MASK)
#elif defined(USE_OPENGL)
#define ADDON_SUPPORTED_RENDMODES (ADDONRM_CLASSIC | ADDONRM_POLYMOST)
#else
#define ADDON_SUPPORTED_RENDMODES (ADDONRM_CLASSIC)
#endif

// reference to another addon
struct addondependency_t
{
    int8_t dflags, cOp;
    char *dependencyId;
    char *version;

    void setFulfilled(bool status)
    {
        if (status) dflags |= 1;
        else dflags &= ~1;
    }
    bool isFulfilled() const { return (bool) (dflags & 1); }

    void cleanup()
    {
        DO_FREE_AND_NULL(dependencyId);
        DO_FREE_AND_NULL(version);
    }
};

struct useraddon_t
{
    // each addon has a name that is displayed in the menu list
    // necessary to allow scrolling of the menu entry
    char menuentryname[ADDON_MAXENTRYNAME];

    char *internalId, *externalId;
    char *data_path, *preview_path;

    // reference to an existing grpfile_t, do not free contents
    const grpfile_t* gamegrpfile;

    // reference to hash table contents, do not free contents
    uint8_t* preview_image_data;

    char *title, *version, *author;
    char *description;

    int32_t gametype;
    int32_t num_gamecrcs;
    int32_t* gamecrcs;

    int32_t content_type, package_type;
    int32_t loadorder_idx;

    char *mscript_path, *mdef_path, *mrts_path;
    char **con_modules, **def_modules;
    int32_t num_con_modules, num_def_modules;

    char **grp_datapaths;
    int32_t num_grp_datapaths;

    char* startmapfilename;
    int32_t startlevel, startvolume;
    uint32_t compatrendmode;

    addondependency_t *dependencies, *incompatibles;
    int32_t num_dependencies, num_incompatibles;

    // these values may be altered after initialization
    uint32_t aflags;
    int32_t missing_deps, active_incompats;

    void cleanup(void)
    {
        menuentryname[0] = '\0';
        DO_FREE_AND_NULL(internalId);
        DO_FREE_AND_NULL(externalId);

        gametype = ADDONGF_NONE;

        content_type = ADDONTYPE_INVALID;
        package_type = ADDONLT_INVALID;
        loadorder_idx = DEFAULT_LOADORDER_IDX;

        DO_FREE_AND_NULL(title);
        DO_FREE_AND_NULL(version);
        DO_FREE_AND_NULL(author);
        DO_FREE_AND_NULL(description);

        DO_FREE_AND_NULL(data_path);
        DO_FREE_AND_NULL(preview_path);
        DO_FREE_AND_NULL(gamecrcs);

        DO_FREE_AND_NULL(mscript_path);
        DO_FREE_AND_NULL(mdef_path);
        DO_FREE_AND_NULL(mrts_path);

        for (int j = 0; j < num_con_modules; j++)
            Xfree(con_modules[j]);
        DO_FREE_AND_NULL(con_modules);
        num_con_modules = 0;

        for (int j = 0; j < num_def_modules; j++)
            Xfree(def_modules[j]);
        DO_FREE_AND_NULL(def_modules);
        num_def_modules = 0;

        for (int j = 0; j < num_grp_datapaths; j++)
            Xfree(grp_datapaths[j]);
        DO_FREE_AND_NULL(grp_datapaths);
        num_grp_datapaths = 0;

        DO_FREE_AND_NULL(startmapfilename);
        startlevel = startvolume = 0;
        compatrendmode = ADDONRM_NONE;

        for (int j = 0; j < num_dependencies; j++)
            dependencies[j].cleanup();
        DO_FREE_AND_NULL(dependencies);
        num_dependencies = 0;

        for (int j = 0; j < num_incompatibles; j++)
            incompatibles[j].cleanup();
        DO_FREE_AND_NULL(incompatibles);
        num_incompatibles = 0;

        aflags = ADDONFLAG_NONE;
        missing_deps = active_incompats = 0;

        // do not free this -- this is a reference to an existing grpfile_t
        gamegrpfile = nullptr;

        // do not free this here -- freed when the hash table is destroyed
        preview_image_data = nullptr;
    }

    void setSelected(bool status)
    {
        if (status) aflags |= ADDONFLAG_SELECTED;
        else aflags &= ~ADDONFLAG_SELECTED;
    }
    bool isSelected() const { return (bool) (aflags & ADDONFLAG_SELECTED); }

    bool isValid() const
    {
        if (!internalId || !externalId ||
            (gametype == ADDONGF_NONE) || (content_type == ADDONTYPE_INVALID) ||
            (package_type == ADDONLT_INVALID) || (compatrendmode == ADDONRM_NONE))
        {
            DLOG_F(ERROR, "Addon '%s' had invalid properties! If you see this error, notify the maintainers.", (internalId) ? internalId : "missing id");
            return false;
        }

        return true;
    }
};

// ----------------------------------------

extern useraddon_t** g_useraddons_grpinfo;
extern int32_t g_addoncount_grpinfo;

#define for_grpaddons(_ptr, _body)\
    for (int _idx = 0; _idx < g_addoncount_grpinfo; _idx++)\
    {\
        useraddon_t* _ptr = g_useraddons_grpinfo[_idx];\
        _body;\
    }

extern useraddon_t** g_useraddons_tcs;
extern int32_t g_addoncount_tcs;

#define for_tcaddons(_ptr, _body)\
    for (int _idx = 0; _idx < g_addoncount_tcs; _idx++)\
    {\
        useraddon_t* _ptr = g_useraddons_tcs[_idx];\
        _body;\
    }

extern useraddon_t** g_useraddons_mods;
extern int32_t g_addoncount_mods;

#define for_modaddons(_ptr, _body)\
    for (int _idx = 0; _idx < g_addoncount_mods; _idx++)\
    {\
        useraddon_t* _ptr = g_useraddons_mods[_idx];\
        _body;\
    }

extern int32_t g_num_selected_addons;
extern int32_t g_num_active_mdeps;
extern int32_t g_num_active_incompats;

extern uint32_t g_addon_compatrendmode;
extern bool g_addon_failedboot;

// ----------------------------------------

void Addon_FreePreviewHashTable(void);
void Addon_LoadPreviewImages(void);
int32_t Addon_LoadPreviewTile(const useraddon_t* addon);

void Addon_PruneInvalidAddons(useraddon_t** & useraddons, int32_t & numuseraddons);
void Addon_InitializeLoadOrders(void);

void Addon_RefreshDependencyStates(void);
void Addon_RefreshPropertyTrackers(void);

#ifdef USE_OPENGL
int32_t Addon_GetBootRendmode(int32_t const rendmode);
#endif

const char* Addon_RetrieveStartMap(int32_t & startlevel, int32_t & startvolume);

int32_t Addon_LoadGrpInfoAddons(void);
int32_t Addon_LoadUserTCs(void);
int32_t Addon_LoadUserMods(void);

int32_t Addon_UpdateMenuEntryName(useraddon_t* addonPtr, const int startidx);

// ----------------------------------------

#ifdef __cplusplus
}
#endif

#endif
#endif