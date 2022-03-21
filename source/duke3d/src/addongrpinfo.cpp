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

// dependency IDs for hardcoded addons
static const char dukevaca_id[] = "dukevaca";
static const char dukedc_id[] = "dukedc";
static const char dukenw_id[] = "dukenw";
static const char dukezone_id[] = "dukezone";
static const char dukepentp_id[] = "dukepentp";

// authors of the hardcoded addons
static const char author_sunstorm[] = "Sunstorm Interactive";
static const char author_sillysoft[] = "Simply Silly Software";
static const char author_intersphere[] = "Intersphere Communications, Ltd. and Tyler Matthews";

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


// set external identity of the grpinfo addon
static void Addon_GrpInfo_SetExternalIdentity(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    const char* identity = nullptr;
    switch (agrpf->type->crcval)
    {
#ifndef EDUKE32_STANDALONE
        case DUKEDC13_CRC:
        case DUKEDCPP_CRC:
        case DUKEDC_CRC:
        case DUKEDC_REPACK_CRC:
            identity = dukedc_id;
            break;
        case VACA13_CRC:
        case VACAPP_CRC:
        case VACA15_CRC:
        case DUKECB_CRC:
        case VACA_REPACK_CRC:
            identity = dukevaca_id;
            break;
        case DUKENW_CRC:
        case DUKENW_DEMO_CRC:
            identity = dukenw_id;
            break;
        case DZ2_13_CRC:
        case DZ2_PP_CRC:
        case DZ2_PP_REPACK_CRC:
            identity = dukezone_id;
            break;
        case PENTP_CRC:
        case PENTP_ZOOM_CRC:
            identity = dukepentp_id;
            break;
#endif
        default:
            break;
    }

    if (identity == nullptr)
        Bsnprintf(addonPtr->jsondat.externalId, ADDON_MAXID, "grpinfo_%x_%d", agrpf->type->crcval, agrpf->type->size);
    else
        Bstrncpyz(addonPtr->jsondat.externalId, identity, ADDON_MAXID);
}

// set author of the grpinfo addon
static void Addon_GrpInfo_SetAuthor(useraddon_t * addonPtr, const grpfile_t * agrpf)
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
            Bstrncpy(addonPtr->jsondat.author, author_sunstorm, ADDON_MAXAUTHOR);
            break;
        case DUKENW_CRC:
        case DUKENW_DEMO_CRC:
        case DZ2_13_CRC:
        case DZ2_PP_CRC:
        case DZ2_PP_REPACK_CRC:
            Bstrncpy(addonPtr->jsondat.author, author_sillysoft, ADDON_MAXAUTHOR);
            break;
        case PENTP_CRC:
        case PENTP_ZOOM_CRC:
            Bstrncpy(addonPtr->jsondat.author, author_intersphere, ADDON_MAXAUTHOR);
            break;
#endif
        default:
            addonPtr->jsondat.author[0] = '\0';
            break;
    }
}

// set description
static void Addon_GrpInfo_SetDescription(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    const char* desc;
    switch (agrpf->type->crcval)
    {
#ifndef EDUKE32_STANDALONE
        case DUKEDC13_CRC:
        case DUKEDCPP_CRC:
        case DUKEDC_CRC:
        case DUKEDC_REPACK_CRC:
            desc = dukedc_description;
            break;
        case VACA13_CRC:
        case VACAPP_CRC:
        case VACA15_CRC:
        case DUKECB_CRC:
        case VACA_REPACK_CRC:
            desc = dukevaca_description;
            break;
        case DUKENW_CRC:
        case DUKENW_DEMO_CRC:
            desc = dukenw_description;
            break;
        case DZ2_13_CRC:
        case DZ2_PP_CRC:
        case DZ2_PP_REPACK_CRC:
            desc = dukezone_description;
            break;
        case PENTP_CRC:
        case PENTP_ZOOM_CRC:
            desc = dukepentp_description;
            break;
#endif
        default:
            desc = "Imported from grpinfo.";
            break;
    }

    const int desclen = strlen(desc) + 1;
    addonPtr->jsondat.description = (char *) Xmalloc(desclen);
    Bstrncpyz(addonPtr->jsondat.description, desc, desclen);
}

// populate the contents of the addon json struct for grpinfo addons
static void Addon_GrpInfo_FakeJson(useraddon_t * addonPtr, const grpfile_t * agrpf)
{
    Addon_GrpInfo_SetExternalIdentity(addonPtr, agrpf);

    // hack: version is set to hex crc
    Bsnprintf(addonPtr->jsondat.version, ADDON_MAXVERSION, "0-%x", agrpf->type->crcval);
    Bstrncpy(addonPtr->jsondat.title, agrpf->type->name, ADDON_MAXTITLE);
    Addon_GrpInfo_SetAuthor(addonPtr, agrpf);
    Addon_GrpInfo_SetDescription(addonPtr, agrpf);
    addonPtr->jsondat.compat_rendmodes = ADDON_RENDMASK;
}

void Addon_FreeGrpInfoAddons(void)
{
    for_grpaddons(addonPtr, { addonPtr->freeContents(); Xfree(addonPtr); });
    DO_FREE_AND_NULL(g_useraddons_grpinfo);
    g_addoncount_grpinfo = 0;
}

// Search for addons in the currently detected grpfiles
void Addon_ReadGrpInfoDescriptors(void)
{
    Addon_FreeGrpInfoAddons();

    int maxaddoncount = 0;
    for (grpfile_t *grp = foundgrps; grp; grp=grp->next)
        if (grp->type->game & GAMEFLAG_ADDON)
            maxaddoncount++;

    g_addoncount_grpinfo = 0;
    g_useraddons_grpinfo = (useraddon_t **) Xcalloc(maxaddoncount, sizeof(useraddon_t*));

    for (const grpfile_t *grp = foundgrps; grp; grp=grp->next)
    {
        if (grp->type->game & GAMEFLAG_ADDON)
        {
            g_useraddons_grpinfo[g_addoncount_grpinfo] = (useraddon_t*) Xcalloc(1, sizeof(useraddon_t));
            useraddon_t* addonPtr = g_useraddons_grpinfo[g_addoncount_grpinfo];
            addonPtr->loadorder_idx = DEFAULT_LOADORDER_IDX;

            addonPtr->internalId = (char*) Xmalloc(ADDON_MAXID);
            Bsnprintf(addonPtr->internalId, ADDON_MAXID, "grpinfo_%x_%d", grp->type->crcval, grp->type->size);
            addonPtr->grpfile = grp;

            addonPtr->loadtype = LT_GRPINFO;
            addonPtr->gametype = (addongame_t) grp->type->game;
            addonPtr->gamecrc = grp->type->dependency;

            Addon_GrpInfo_FakeJson(addonPtr, grp);
            addonPtr->setSelected(g_selectedGrp == grp);
            g_addoncount_grpinfo++;
        }
    }

    g_useraddons_grpinfo = (useraddon_t **) Xrealloc(g_useraddons_grpinfo, g_addoncount_grpinfo * sizeof(useraddon_t*));
}
