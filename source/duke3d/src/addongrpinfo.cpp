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

#include "duke3d.h"
#include "addons.h"

// addons loaded from .grpinfo files. Mutually exclusive and replace the selected GRP.
useraddon_t** g_useraddons_grpinfo = nullptr;
int32_t g_addoncount_grpinfo = 0;

// external dependency IDs for hardcoded addons
static const char dukevaca_id[] = "dukevaca";
static const char dukedc_id[] = "dukedc";
static const char dukenw_id[] = "dukenw";
static const char dukezone_id[] = "dukezone";
static const char dukepentp_id[] = "dukepentp";

// authors of the hardcoded addons
static const char author_sunstorm[] = "Sunstorm Interactive";
static const char author_sillysoft[] = "Simply Silly Software";
static const char author_intersphere[] = "Intersphere Communications, Ltd. and Tyler Matthews";

// default description for grpinfo imported addons
static const char grpinfo_description[] = "Imported from grpinfo.";

// descriptions for hardcoded addons (taken from the back of the box or READMEs and adapted)
static const char dukevaca_description[] =
    "Ahhh... the Caribbean, the ultimate vacation destination.\n"
    "After a few months of alien annihilation, Duke's ready for a little R&R. "
    "Cabana girls, a beach-side bar and bermuda shorts are all he needs. "
    "That is, until the alien scum drop in for a little vacation of their own...";

static const char dukedc_description[] =
    "Aliens have captured the President!\n"
    "Duke gets word that alien scum have landed in Washington D.C., "
    "laid it to waste, and imprisoned the leader of the free world. "
    "Always up for a heroic deed, Duke heads to D.C. to rid the city "
    "of enemy dirtbags and return the president to power!";

static const char dukenw_description[] =
    "There's diabolical danger in the northern Ice-Land!\n"
    "Alien scum have taken over, and the fate of everyone's favorite jolly old man "
    "and his village of merry little ones hinges on an icy rescue. The Winter "
    "Wonderland will never be the same once Duke's begun the Arctic Meltdown.";

static const char dukezone_description[] =
    "Features 3 new episodes that contain 7 levels each. These maps take Duke "
    "across urban arctic wastelands, underground passages, canyons, fun houses, "
    "bars and a toxic chemical processing plant.\n"
    "Does not include the 500 levels packaged with the original release of the addon.";

static const char dukepentp_description[] =
    "Set between the third and fourth episode of Duke Nukem 3D.\n"
    "While Duke was trying to establish a little \"beach-head,\" the aliens have "
    "dropped in to break up his fun in the sun and spoil a couple of Penthouse photo "
    "shoots to boot. It's up to Duke Nukem to save the day - again.";


static void inline Addon_GrpInfo_SetInternalIdentity(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    Bsnprintf(tempbuf, 32, "grpinfo_%x_%d", agrpf->type->crcval, agrpf->type->size);
    addonPtr->internalId = Xstrdup(tempbuf);
}

static void inline Addon_GrpInfo_SetExternalIdentity(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    switch (agrpf->type->crcval)
    {
#ifndef EDUKE32_STANDALONE
        case DUKEDC13_CRC:
        case DUKEDCPP_CRC:
        case DUKEDC_CRC:
        case DUKEDC_REPACK_CRC:
            addonPtr->externalId = Xstrdup(dukedc_id);
            break;
        case VACA13_CRC:
        case VACAPP_CRC:
        case VACA15_CRC:
        case DUKECB_CRC:
        case VACA_REPACK_CRC:
            addonPtr->externalId = Xstrdup(dukevaca_id);
            break;
        case DUKENW_CRC:
        case DUKENW_DEMO_CRC:
            addonPtr->externalId = Xstrdup(dukenw_id);
            break;
        case DZ2_13_CRC:
        case DZ2_PP_CRC:
        case DZ2_PP_REPACK_CRC:
            addonPtr->externalId = Xstrdup(dukezone_id);
            break;
        case PENTP_CRC:
        case PENTP_ZOOM_CRC:
            addonPtr->externalId = Xstrdup(dukepentp_id);
            break;
#endif
        default:
            // same as internal
            Bsnprintf(tempbuf, 32, "grpinfo_%x_%d", agrpf->type->crcval, agrpf->type->size);
            addonPtr->externalId = Xstrdup(tempbuf);
            break;
    }
}

static void inline Addon_GrpInfo_SetVersion(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    // hack: version is set to hex crcval
    Bsnprintf(tempbuf, 16, "0-%x", agrpf->type->crcval);
    addonPtr->version = Xstrdup(tempbuf);
}

static void inline Addon_GrpInfo_SetGameDependency(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    addonPtr->gametype = agrpf->type->game;
    addonPtr->gamecrc = agrpf->type->dependency;
}

static void inline Addon_GrpInfo_SetTitle(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    addonPtr->title = Xstrdup(agrpf->type->name);
}

static void inline Addon_GrpInfo_SetAuthor(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    switch (agrpf->type->crcval)
    {
#ifndef EDUKE32_STANDALONE
        case DUKEDC13_CRC:
        case DUKEDCPP_CRC:
        case DUKEDC_CRC:
        case DUKEDC_REPACK_CRC:
        case VACA13_CRC:
        case VACAPP_CRC:
        case VACA15_CRC:
        case DUKECB_CRC:
        case VACA_REPACK_CRC:
            addonPtr->author = Xstrdup(author_sunstorm);
            break;
        case DUKENW_CRC:
        case DUKENW_DEMO_CRC:
        case DZ2_13_CRC:
        case DZ2_PP_CRC:
        case DZ2_PP_REPACK_CRC:
            addonPtr->author = Xstrdup(author_sillysoft);
            break;
        case PENTP_CRC:
        case PENTP_ZOOM_CRC:
            addonPtr->author = Xstrdup(author_intersphere);
            break;
#endif
        default:
            addonPtr->author = nullptr;
            break;
    }
}

static void inline Addon_GrpInfo_SetDescription(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    switch (agrpf->type->crcval)
    {
#ifndef EDUKE32_STANDALONE
        case DUKEDC13_CRC:
        case DUKEDCPP_CRC:
        case DUKEDC_CRC:
        case DUKEDC_REPACK_CRC:
            addonPtr->description = Xstrdup(dukedc_description);
            break;
        case VACA13_CRC:
        case VACAPP_CRC:
        case VACA15_CRC:
        case DUKECB_CRC:
        case VACA_REPACK_CRC:
            addonPtr->description = Xstrdup(dukevaca_description);
            break;
        case DUKENW_CRC:
        case DUKENW_DEMO_CRC:
            addonPtr->description = Xstrdup(dukenw_description);
            break;
        case DZ2_13_CRC:
        case DZ2_PP_CRC:
        case DZ2_PP_REPACK_CRC:
            addonPtr->description = Xstrdup(dukezone_description);
            break;
        case PENTP_CRC:
        case PENTP_ZOOM_CRC:
            addonPtr->description = Xstrdup(dukepentp_description);
            break;
#endif
        default:
            addonPtr->description = Xstrdup(grpinfo_description);
            break;
    }
}

static void inline Addon_GrpInfo_CheckOfficial(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    switch (agrpf->type->crcval)
    {
#ifndef EDUKE32_STANDALONE
        case DUKEDC13_CRC:
        case DUKEDCPP_CRC:
        case DUKEDC_CRC:
        case DUKEDC_REPACK_CRC:
        case VACA13_CRC:
        case VACAPP_CRC:
        case VACA15_CRC:
        case DUKECB_CRC:
        case VACA_REPACK_CRC:
        case DUKENW_CRC:
        case DUKENW_DEMO_CRC:
        case DZ2_13_CRC:
        case DZ2_PP_CRC:
        case DZ2_PP_REPACK_CRC:
        case PENTP_CRC:
        case PENTP_ZOOM_CRC:
            addonPtr->aflags |= ADDONFLAG_OFFICIAL;
            break;
#endif
        default:
            addonPtr->aflags &= ~ADDONFLAG_OFFICIAL;
            break;
    }
}


// populate the contents of the addon json struct for grpinfo addons
static int32_t Addon_GrpInfo_ParseDescriptor(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    addonPtr->grpfile = agrpf;

    Addon_GrpInfo_SetInternalIdentity(addonPtr, agrpf);
    Addon_GrpInfo_SetExternalIdentity(addonPtr, agrpf);
    Addon_GrpInfo_SetVersion(addonPtr, agrpf);

    Addon_GrpInfo_SetGameDependency(addonPtr, agrpf);
    Addon_GrpInfo_CheckOfficial(addonPtr, agrpf);

    Addon_GrpInfo_SetTitle(addonPtr, agrpf);
    Addon_GrpInfo_SetAuthor(addonPtr, agrpf);
    Addon_GrpInfo_SetDescription(addonPtr, agrpf);

    return 0;
}

// Iterate over all grpfiles, add ones with GAMEFLAG_ADDON to the eligible menu addons
void Addon_ReadGrpInfoDescriptors(void)
{
    Addon_FreeGrpInfoAddons();

    // count maximum potential grpfile addons to allocate space
    int maxaddoncount = 0;
    for (grpfile_t *grp = foundgrps; grp; grp=grp->next)
        if (grp->type->game & GAMEFLAG_ADDON)
            maxaddoncount++;

    // allocate memory for them, will later be refitted
    g_useraddons_grpinfo = (useraddon_t **) Xcalloc(maxaddoncount, sizeof(useraddon_t*));
    g_addoncount_grpinfo = 0;

    // construct actual addon structs
    for (const grpfile_t *grp = foundgrps; grp; grp=grp->next)
    {
        if (grp->type->game & GAMEFLAG_ADDON)
        {
            // absolutely MUST be zero-initialized!
            g_useraddons_grpinfo[g_addoncount_grpinfo] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));
            useraddon_t* & addonPtr = g_useraddons_grpinfo[g_addoncount_grpinfo];

            addonPtr->content_type = ADDONTYPE_GRPINFO;
            addonPtr->package_type = ADDONLT_GRPINFO;

            // grpfile addons always compatible with all rendmodes, no load order
            addonPtr->compatrendmode = ADDONRM_MASK;
            addonPtr->loadorder_idx = DEFAULT_LOADORDER_IDX;

            if (EDUKE32_PREDICT_FALSE(Addon_GrpInfo_ParseDescriptor(addonPtr, grp)))
            {
                addonPtr->cleanup();
                DO_FREE_AND_NULL(addonPtr);
                continue;
            }

            // grpfile addons are selected if the grpfile is currently active
            addonPtr->setSelected(g_selectedGrp == grp);
            g_addoncount_grpinfo++;
        }
    }

    // we have final set of addons
    g_useraddons_grpinfo = (useraddon_t **) Xrealloc(g_useraddons_grpinfo, g_addoncount_grpinfo * sizeof(useraddon_t*));
}

// clean up previously allocated grpinfo addons
void Addon_FreeGrpInfoAddons(void)
{
    for_grpaddons(addonPtr,
    {
        addonPtr->cleanup();
        Xfree(addonPtr);
    });
    DO_FREE_AND_NULL(g_useraddons_grpinfo);
    g_addoncount_grpinfo = 0;
}
