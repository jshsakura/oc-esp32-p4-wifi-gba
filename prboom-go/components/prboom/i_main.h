/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *      General system functions. Signal related stuff, exit function
 *      prototypes, and programmable Doom clock.
 *
 *-----------------------------------------------------------------------------
 */

#ifndef __I_MAIN__
#define __I_MAIN__

void I_Init(void);
void I_SafeExit(int rc);

#ifdef RETRO_GO
#include <setjmp.h>

/* I_Error() is the engine's only "this is fatal, stop now" signal -- it is
 * called both for a missing/corrupt WAD at startup and for unrelated fatal
 * errors deep in a running game, and it never returns to its caller. Rather
 * than add a checked return path to every call site between D_DoomMain() and
 * the WAD loader (which is most of the engine), the retro-go glue installs a
 * jump point here before calling D_DoomMain(); I_Error() longjmps to it with
 * a message instead of aborting, so the failure can be reported to the user
 * and returned to the launcher like any other bad ROM. */
extern jmp_buf i_error_recovery_point;
extern char i_error_recovery_msg[256];
#endif

#endif
