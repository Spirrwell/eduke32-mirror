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
#define MAXLEN_ADDONAUTHOR 24
#define MAXLEN_ADDONVERSION 24
#define MAXLEN_ADDONDESC 8192

struct addonbrief_t
{
    addonbrief_t()
    {
        reset();
    }
    addonbrief_t(char const *n)
    {
        strncpy(name, n, MAXSAVEGAMENAME);
        path[0] = '\0';
    }

    char name[MAXSAVEGAMENAMESTRUCT];
    char path[BMAX_PATH];
    uint8_t isExt = 0;

    void reset()
    {
        name[0] = '\0';
        path[0] = '\0';
        isExt = 0;
    }
    bool isValid() const
    {
        return path[0] != '\0';
    }
};

struct menuaddon_t
{
    savebrief_t brief;

    union
    {
        struct
        {
            int isAutoSave     : 1;
            int isOldScriptVer : 1;
            int isOldVer       : 1;
            int isUnreadable   : 1;
        };
        uint8_t flags;
    };

    void clear()
    {
        brief.reset();
        flags = 0;
    }
};

extern menuaddon_t * g_menuaddons;
extern uint16_t g_nummenuaddons;

extern char m_addondescbuf[MAXLEN_ADDONDESC];
extern char m_addontitlebuf[MAXLEN_ADDONTITLE];
extern char m_addonauthorbuf[MAXLEN_ADDONAUTHOR];
extern char m_addonversionbuf[MAXLEN_ADDONVERSION];

extern int32_t m_addondesc_lbcount;
extern int32_t m_addondesc_shift;

void addontextwrap(const char *desc, int32_t lblen);

#ifdef __cplusplus
}
#endif

#endif