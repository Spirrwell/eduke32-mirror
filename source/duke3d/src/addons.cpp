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

#include "vfs.h"

menuaddon_t * g_internaladdons;
menuaddon_t * g_menuaddons;
uint16_t g_nummenuaddons;

char m_addontitlebuf[MAXLEN_ADDONTITLE] = "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM";
char m_addonauthorbuf[MAXLEN_ADDONAUTHOR] = "MMMMMMMMMMMMMMMMMMMMMMM";
char m_addonversionbuf[MAXLEN_ADDONVERSION] = "MMMMMMMMMMMMMMM";
char m_addondescbuf[MAXLEN_ADDONDESC] = "";

int32_t g_numinternaladdons;
int32_t m_addondesc_lbcount = 0;
int32_t m_addondesc_shift = 0;

void addontextwrap(const char *desc, int32_t lblen)
{
    int orig_pos = 0;
    int buf_pos = 0;
    int clen = 0;
    int lastws = 0;
    m_addondesc_lbcount = 1;
    while (desc[orig_pos] && (buf_pos < MAXLEN_ADDONDESC - 8))
    {
        if (isspace(desc[orig_pos]))
            lastws = buf_pos;

        m_addondescbuf[buf_pos++] = desc[orig_pos++];

        if (desc[orig_pos-1] == '\n')
        {
            m_addondesc_lbcount++;
            clen = 0;
        }
        else if (++clen >= lblen)
        {
            if (buf_pos - lastws < lblen/4)
            {
                // split at whitespace
                m_addondescbuf[lastws] = '\n';
                clen = buf_pos - lastws;
            }
            else
            {
                // split word
                m_addondescbuf[buf_pos++] = '-';
                m_addondescbuf[buf_pos++] = '\n';
                clen = 0;
            }
            m_addondesc_lbcount++;
            lastws = 0;
        }
    }

    if (buf_pos >= MAXLEN_ADDONDESC - 8)
        Bstrcpy(&m_addondescbuf[buf_pos], " [...]");
    else
        m_addondescbuf[buf_pos] = '\0';
}


static void ReadAddonPackages_CACHE1D(BUILDVFS_FIND_REC *f)
{
    /*
    savehead_t h;

    for (; f != nullptr; f = f->next)
    {
        char const * fn = f->name;
        buildvfs_kfd fil = kopen4loadfrommod(fn, 0);
        if (fil == buildvfs_kfd_invalid)
            continue;

        initgroupfile(fn);

        popgroupfile();

        menuaddon_t & madd = g_internaladdons[g_numinternaladdons];

        int32_t k = sv_loadheader(fil, 0, &h);
        if (k)
        {
            if (k < 0)
                msv.isUnreadable = 1;
            else
            {
                if (FURY)
                {
                    char extfn[BMAX_PATH];
                    snprintf(extfn, ARRAY_SIZE(extfn), "%s.ext", fn);
                    buildvfs_kfd extfil = kopen4loadfrommod(extfn, 0);
                    if (extfil != buildvfs_kfd_invalid)
                    {
                        msv.brief.isExt = 1;
                        kclose(extfil);
                    }
                }
            }
            msv.isOldVer = 1;
        }
        else
            msv.isOldVer = 0;

        msv.isAutoSave = h.isAutoSave();
        msv.isOldScriptVer = h.userbytever < ud.userbytever;

        strncpy(msv.brief.path, fn, ARRAY_SIZE(msv.brief.path));
        ++g_numinternalsaves;

        if (k >= 0 && h.savename[0] != '\0')
        {
            memcpy(msv.brief.name, h.savename, ARRAY_SIZE(msv.brief.name));
        }
        else
            msv.isUnreadable = 1;

        kclose(fil);
    }*/
}

void ReadAddonPackages(void)
{
/*
    static char const DefaultPath[] = "/addons";

    BUILDVFS_FIND_REC *zipfiles_addons = klistpath(DefaultPath, "*.zip", BUILDVFS_FIND_FILE);
    BUILDVFS_FIND_REC *grpfiles_addons = klistpath(DefaultPath, "*.grp", BUILDVFS_FIND_FILE);
    BUILDVFS_FIND_REC *pk3files_addons = klistpath(DefaultPath, "*.pk3", BUILDVFS_FIND_FILE);
    BUILDVFS_FIND_REC *pk4files_addons = klistpath(DefaultPath, "*.pk4", BUILDVFS_FIND_FILE);

    int numfiles = 0;
    auto countfiles = [](int & count, BUILDVFS_FIND_REC *f) { for (; f != nullptr; f = f->next) count++; };
    countfiles(numfiles, zipfiles_addons);
    countfiles(numfiles, grpfiles_addons);
    countfiles(numfiles, pk3files_addons);
    countfiles(numfiles, pk4files_addons);

    size_t const totaladdonsize = sizeof(menuaddon_t) * numfiles;
    g_internaladdons = (menuaddon_t *)Xrealloc(g_internaladdons, totaladdonsize);

    for (int x = 0; x < numfiles; ++x)
        g_internaladdons[x].clear();

    g_numinternaladdons = 0;
    ReadAddonPackages_CACHE1D(findfiles_default);
    klistfree(zipfiles_addons);
    klistfree(grpfiles_addons);
    klistfree(files_addons);
    klistfree(grpfiles_addons);


    g_nummenusaves = 0;
    for (int x = g_numinternalsaves-1; x >= 0; --x)
    {
        menusave_t & msv = g_internalsaves[x];
        if (!msv.isUnreadable)
        {
            ++g_nummenusaves;
        }
    }
    size_t const menusavesize = sizeof(menusave_t) * g_nummenusaves;

    g_menusaves = (menusave_t *)Xrealloc(g_menusaves, menusavesize);

    for (int x = 0; x < g_nummenusaves; ++x)
        g_menusaves[x].clear();

    for (int x = g_numinternalsaves-1, y = 0; x >= 0; --x)
    {
        menusave_t & msv = g_internalsaves[x];
        if (!msv.isUnreadable)
        {
            g_menusaves[y++] = msv;
        }
    }

    for (int x = g_numinternalsaves-1; x >= 0; --x)
    {
        char const * const path = g_internalsaves[x].brief.path;
        int const pathlen = Bstrlen(path);
        if (pathlen < 12)
            continue;
        char const * const fn = path + (pathlen-12);
        if (fn[0] == 's' && fn[1] == 'a' && fn[2] == 'v' && fn[3] == 'e' &&
            isdigit(fn[4]) && isdigit(fn[5]) && isdigit(fn[6]) && isdigit(fn[7]))
        {
            char number[5];
            memcpy(number, fn+4, 4);
            number[4] = '\0';
            savecounter.count = Batoi(number)+1;
            break;
        }
    }
    */
}
