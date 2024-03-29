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

#include "compat.h"

void dukeConsolePrintChar(int x, int y, char ch, int shade, int pal);
void dukeConsolePrintString(int x, int y, const char *ch, int len, int shade, int pal);
void dukeConsolePrintCursor(int x, int y, int type, int32_t lastkeypress);
int dukeConsoleGetColumnWidth(int w);
int dukeConsoleGetRowHeight(int h);
void dukeConsoleOnShowCallback(int shown);
void dukeConsoleClearBackground(int numcols, int numrows);

extern int osdhightile;
extern float osdscale, osdrscale;
