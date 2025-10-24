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

#ifndef gameexec_h_
#define gameexec_h_

#include "build.h"
#include "events_defs.h"
#include "gamedef.h"  // vmstate_t
#include "sector.h"  // mapstate_t

typedef struct
{
    uint16_t zsum;
    uint16_t hi, lo;
} zhit_t;

extern zhit_t zhit[MAXSPRITES];

int32_t VM_ExecuteEvent(int const nEventID, int const spriteNum, int const playerNum, int const nDist, int32_t const nReturn);
int32_t VM_ExecuteEvent(int const nEventID, int const spriteNum, int const playerNum, int const nDist);
int32_t VM_ExecuteEvent(int const nEventID, int const spriteNum, int const playerNum);
int32_t VM_ExecuteEventWithValue(int const nEventID, int const spriteNum, int const playerNum, int32_t const nReturn);

static FORCE_INLINE int VM_HaveEvent(int const nEventID)
{
    return !!apScriptEvents[nEventID];
}

static FORCE_INLINE int32_t VM_OnEvent(int nEventID, int spriteNum, int playerNum, int nDist, int32_t nReturn)
{
    return VM_HaveEvent(nEventID) ? VM_ExecuteEvent(nEventID, spriteNum, playerNum, nDist, nReturn) : nReturn;
}

static FORCE_INLINE int32_t VM_OnEvent(int nEventID, int spriteNum, int playerNum, int nDist)
{
    return VM_HaveEvent(nEventID) ? VM_ExecuteEvent(nEventID, spriteNum, playerNum, nDist) : 0;
}

static FORCE_INLINE int32_t VM_OnEvent(int nEventID, int spriteNum = -1, int playerNum = -1)
{
    return VM_HaveEvent(nEventID) ? VM_ExecuteEvent(nEventID, spriteNum, playerNum) : 0;
}

static FORCE_INLINE int32_t VM_OnEventWithReturn(int nEventID, int spriteNum, int playerNum, int32_t nReturn)
{
    return VM_HaveEvent(nEventID) ? VM_ExecuteEventWithValue(nEventID, spriteNum, playerNum, nReturn) : nReturn;
}

#ifdef __cplusplus
extern "C" {
#endif

enum vmflags_t
{
    VM_RETURN     = 1<<0,
    VM_KILL       = 1<<1,
    VM_EXIT       = 1<<2,
    VM_TERMINATE  = 1<<3,
};

extern int32_t ticrandomseed;

extern vmstate_t vm;
extern int32_t g_tw;
extern int32_t g_currentEvent;

void A_LoadActor(int const spriteNum);

void A_Execute(int spriteNum, int playerNum, int playerDist);
void A_Fall(int spriteNum);
int A_GetFurthestAngle(int const spriteNum, int const angDiv);
void A_GetZLimits(int spriteNum);
int __fastcall G_GetAngleDelta(int currAngle, int newAngle);
void G_RestoreMapState();
void G_SaveMapState();

void VM_DrawTileGeneric(int32_t x, int32_t y, int32_t zoom, int32_t tilenum,
    int32_t shade, int32_t orientation, int32_t p);

void VM_DrawTile(int32_t x, int32_t y, int32_t tilenum, int32_t shade, int32_t orientation);
static inline void VM_DrawTilePal(int32_t x, int32_t y, int32_t tilenum, int32_t shade, int32_t orientation, int32_t p)
{
    VM_DrawTileGeneric(x, y, 65536, tilenum, shade, orientation, p);
}
static inline void VM_DrawTilePalSmall(int32_t x, int32_t y, int32_t tilenum, int32_t shade, int32_t orientation, int32_t p)
{
    VM_DrawTileGeneric(x, y, 32768, tilenum, shade, orientation, p);
}
void VM_DrawTileSmall(int32_t x, int32_t y, int32_t tilenum, int32_t shade, int32_t orientation);

#define CON_ERRPRINTF(Text, ...) do { \
    vm.flags |= VM_RETURN; \
    LOG_F(ERROR, "%s:%d: %s: " Text, VM_FILENAME(insptr), VM_DECODE_LINE_NUMBER(g_tw), VM_GetKeywordForID(VM_DECODE_INST(g_tw)), ## __VA_ARGS__); \
} while (0)

#define CON_CRITICALERRPRINTF(Text, ...) do { \
    vm.flags |= VM_RETURN; \
    LOG_F(ERROR, "%s:%d: %s: " Text, VM_FILENAME(insptr), VM_DECODE_LINE_NUMBER(g_tw), VM_GetKeywordForID(VM_DECODE_INST(g_tw)), ## __VA_ARGS__); \
    wm_msgbox(APPNAME, "%s:%d: %s: " Text, VM_FILENAME(insptr), VM_DECODE_LINE_NUMBER(g_tw), VM_GetKeywordForID(VM_DECODE_INST(g_tw)), ## __VA_ARGS__); \
} while (0)

void G_GetTimeDate(int32_t * pValues);
int G_StartTrack(int levelNum);

void VM_UpdateAnim(int const spriteNum, int32_t * const pData);
void VM_GetZRange(int const spriteNum, int32_t * const ceilhit, int32_t * const florhit, int const wallDist);

#ifndef NDEBUG
#define MAX_CON_DEBUG_LINES 128
struct _CON_DEBUG_LINES
{
    int32_t line;
    intptr_t offset;
};
struct _CON_NONDEBUG_LINES
{
    int32_t line;
    char filename[256];
};
extern struct _CON_DEBUG_LINES con_debug_lines[MAX_CON_DEBUG_LINES];
extern uint8_t con_debug_num_lines;
extern bool con_debug_line_by_line;
extern bool vm_sandBox;
bool VM_CONSetDebugLine(int32_t line, const char* filename);
bool VM_CONUnsetDebugLine(int32_t line, const char* filename);
void VM_DebugSandBox(intptr_t const *inst);
bool VM_CONCheckDebugLine(int32_t line, const char *filename);
#endif

#ifdef __cplusplus
}
#endif

#endif
