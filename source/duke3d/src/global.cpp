//-------------------------------------------------------------------------
/*
Copyright (C) 2010 EDuke32 developers and contributors

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

#define global_c_
#include "global.h"
#include "duke3d.h"

user_defs ud;

const char *s_buildDate = "20120522";

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EDUKE32_STANDALONE
#define DEFAULT_VOLUMENAMES { "L.A. Meltdown", "Lunar Apocalypse", "Shrapnel City" }
#define DEFAULT_SKILLNAMES { "Piece Of Cake", "Let's Rock", "Come Get Some", "Damn I'm Good" }
#define DEFAULT_GAMETYPES { "DukeMatch (Spawn)", "Cooperative Play", "DukeMatch (No Spawn)", "Team DM (Spawn)", "Team DM (No Spawn)" }
#else
#define DEFAULT_VOLUMENAMES { }
#define DEFAULT_SKILLNAMES { }
#define DEFAULT_GAMETYPES { "Deathmatch (Spawn)", "Cooperative Play", "Deathmatch (No Spawn)", "Team DM (Spawn)", "Team DM (No Spawn)" }
#endif

#define  GTFLAGS_DEATHMATCH_SPAWN \
    GAMETYPE_FRAGBAR | \
    GAMETYPE_SCORESHEET | \
    GAMETYPE_DMSWITCHES | \
    GAMETYPE_ITEMRESPAWN | \
    GAMETYPE_MARKEROPTION | \
    GAMETYPE_ACCESSATSTART

#define  GTFLAGS_COOPERATIVE \
    GAMETYPE_COOP | \
    GAMETYPE_WEAPSTAY | \
    GAMETYPE_COOPSPAWN | \
    GAMETYPE_ACCESSCARDSPRITES | \
    GAMETYPE_COOPVIEW | \
    GAMETYPE_COOPSOUND | \
    GAMETYPE_OTHERPLAYERSINMAP | \
    GAMETYPE_PLAYERSFRIENDLY | \
    GAMETYPE_FIXEDRESPAWN | \
    GAMETYPE_PRESERVEINVENTORYDEATH

#define  GTFLAGS_DEATHMATCH_NOSPAWN \
    GAMETYPE_WEAPSTAY | \
    GAMETYPE_FRAGBAR | \
    GAMETYPE_SCORESHEET | \
    GAMETYPE_DMSWITCHES | \
    GAMETYPE_ACCESSATSTART

#define  GTFLAGS_TEAMDEATHMATCH_SPAWN \
    GAMETYPE_FRAGBAR | \
    GAMETYPE_SCORESHEET | \
    GAMETYPE_DMSWITCHES | \
    GAMETYPE_ITEMRESPAWN | \
    GAMETYPE_MARKEROPTION | \
    GAMETYPE_ACCESSATSTART | \
    GAMETYPE_TDM | \
    GAMETYPE_TDMSPAWN

#define  GTFLAGS_TEAMDEATHMATCH_NOSPAWN \
    GAMETYPE_WEAPSTAY | \
    GAMETYPE_FRAGBAR | \
    GAMETYPE_SCORESHEET | \
    GAMETYPE_DMSWITCHES | \
    GAMETYPE_ACCESSATSTART | \
    GAMETYPE_TDM | \
    GAMETYPE_TDMSPAWN

#define DEFAULT_GTFLAGS \
{\
    GTFLAGS_DEATHMATCH_SPAWN,\
    GTFLAGS_COOPERATIVE,\
    GTFLAGS_DEATHMATCH_NOSPAWN,\
    GTFLAGS_TEAMDEATHMATCH_SPAWN,\
    GTFLAGS_TEAMDEATHMATCH_NOSPAWN\
}

#define DEFAULT_BLIMPSPAWNITEMS \
{\
    RPGSPRITE__,\
    CHAINGUNSPRITE__,\
    DEVISTATORAMMO__,\
    RPGAMMO__,\
    RPGAMMO__,\
    JETPACK__,\
    SHIELD__,\
    FIRSTAID__,\
    STEROIDS__,\
    RPGAMMO__,\
    RPGAMMO__,\
    RPGSPRITE__,\
    RPGAMMO__,\
    FREEZESPRITE__,\
    FREEZEAMMO__\
}

#define DEFAULT_CHEATKEYS { sc_D, sc_N }

#define DEFAULT_ACTORRESPAWNTIME 768
#define DEFAULT_BOUNCEMINERADIUS 2500
#define DEFAULT_DELETEQUEUESIZE  64
#define DEFAULT_ITEMRESPAWNTIME  768

#define DEFAULT_MORTERRADIUS     2500
#define DEFAULT_FREEZEBOUNCES    3
#define DEFAULT_GAMETYPECNT      5
#define DEFAULT_VOLUMECNT        3
#define DEFAULT_PIPEBOMBRADIUS   2500
#define DEFAULT_PLAYERFRICTION   0xCFD0
#define DEFAULT_RPGRADIUS        1780
#define DEFAULT_SCRIPTSIZE       1048576
#define DEFAULT_SEENINERADIUS    2048
#define DEFAULT_SHRINKERRADIUS   650
#define DEFAULT_SPRITEGRAVITY    176
#define DEFAULT_TRIPBOMBRADIUS   3880

static char s_startupVolumeNames[MAXVOLUMES][33] = DEFAULT_VOLUMENAMES;
static char s_startupSkillNames[MAXSKILLS][33] = DEFAULT_SKILLNAMES;
static char s_startupGametypeNames[MAXGAMETYPES][33] = DEFAULT_GAMETYPES;

static int32_t s_startupGametypeFlags[MAXGAMETYPES] = DEFAULT_GTFLAGS;
static int16_t s_startupBlimpSpawnItems[15] = DEFAULT_BLIMPSPAWNITEMS;
static char s_startupCheatKeys[2] = DEFAULT_CHEATKEYS;

char    g_volumeNames[MAXVOLUMES][33] = DEFAULT_VOLUMENAMES;
char    g_skillNames[MAXSKILLS][33] = DEFAULT_SKILLNAMES;
char    g_gametypeNames[MAXGAMETYPES][33] = DEFAULT_GAMETYPES;

int32_t g_volumeFlags[MAXVOLUMES] = {};

int32_t g_gametypeFlags[MAXGAMETYPES] = DEFAULT_GTFLAGS;

int32_t g_frameStackSize     = DRAWFRAME_DEFAULT_STACK_SIZE;

int32_t g_actorRespawnTime   = DEFAULT_ACTORRESPAWNTIME;
int32_t g_bouncemineRadius   = DEFAULT_BOUNCEMINERADIUS;
int32_t g_deleteQueueSize    = DEFAULT_DELETEQUEUESIZE;
int32_t g_itemRespawnTime    = DEFAULT_ITEMRESPAWNTIME;

int32_t g_morterRadius       = DEFAULT_MORTERRADIUS;
int32_t g_numFreezeBounces   = DEFAULT_FREEZEBOUNCES;
int32_t g_gametypeCnt        = DEFAULT_GAMETYPECNT;
int32_t g_volumeCnt          = DEFAULT_VOLUMECNT;
int32_t g_pipebombRadius     = DEFAULT_PIPEBOMBRADIUS;
int32_t g_playerFriction     = DEFAULT_PLAYERFRICTION;
int32_t g_rpgRadius          = DEFAULT_RPGRADIUS;
int32_t g_scriptSize         = DEFAULT_SCRIPTSIZE;
int32_t g_seenineRadius      = DEFAULT_SEENINERADIUS;
int32_t g_shrinkerRadius     = DEFAULT_SHRINKERRADIUS;
int32_t g_spriteGravity      = DEFAULT_SPRITEGRAVITY;
int32_t g_timerTicsPerSecond = TICRATE;
int32_t g_tripbombRadius     = DEFAULT_TRIPBOMBRADIUS;

int16_t g_blimpSpawnItems[15] = DEFAULT_BLIMPSPAWNITEMS;

char CheatKeys[2]       = DEFAULT_CHEATKEYS;

char g_setupFileName[BMAX_PATH] = SETUPFILENAME;

void G_ResetGlobalVars(void)
{
    Bmemcpy(g_volumeNames, s_startupVolumeNames, sizeof(g_volumeNames));
    Bmemcpy(g_skillNames, s_startupSkillNames, sizeof(s_startupSkillNames));
    Bmemcpy(g_gametypeNames, s_startupGametypeNames, sizeof(g_gametypeNames));

    Bmemset(g_volumeFlags, 0, sizeof(g_volumeFlags));
    Bmemcpy(g_gametypeFlags, s_startupGametypeFlags, sizeof(g_gametypeFlags));

    g_frameStackSize     = DRAWFRAME_DEFAULT_STACK_SIZE;

    g_actorRespawnTime   = DEFAULT_ACTORRESPAWNTIME;
    g_bouncemineRadius   = DEFAULT_BOUNCEMINERADIUS;
    g_deleteQueueSize    = DEFAULT_DELETEQUEUESIZE;
    g_itemRespawnTime    = DEFAULT_ITEMRESPAWNTIME;

    g_morterRadius       = DEFAULT_MORTERRADIUS;
    g_numFreezeBounces   = DEFAULT_FREEZEBOUNCES;
    g_gametypeCnt        = DEFAULT_GAMETYPECNT;
    g_volumeCnt          = DEFAULT_VOLUMECNT;
    g_pipebombRadius     = DEFAULT_PIPEBOMBRADIUS;
    g_playerFriction     = DEFAULT_PLAYERFRICTION;
    g_rpgRadius          = DEFAULT_RPGRADIUS;
    g_scriptSize         = DEFAULT_SCRIPTSIZE;
    g_seenineRadius      = DEFAULT_SEENINERADIUS;
    g_shrinkerRadius     = DEFAULT_SHRINKERRADIUS;
    g_spriteGravity      = DEFAULT_SPRITEGRAVITY;
    g_timerTicsPerSecond = TICRATE;
    g_tripbombRadius     = DEFAULT_TRIPBOMBRADIUS;

    Bmemcpy(g_blimpSpawnItems, s_startupBlimpSpawnItems, sizeof(g_blimpSpawnItems));
    CheatKeys[0] = s_startupCheatKeys[0];
    CheatKeys[1] = s_startupCheatKeys[1];

    Bstrncpy(g_setupFileName, SETUPFILENAME, sizeof(g_setupFileName));
}

#ifdef __cplusplus
}
#endif
