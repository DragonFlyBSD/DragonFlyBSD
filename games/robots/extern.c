/*-
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#)extern.c	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/games/robots/extern.c,v 1.2 1999/11/30 03:49:16 billf Exp $
 * $DragonFly: src/games/robots/extern.c,v 1.3 2006/08/27 21:45:07 pavalos Exp $
 */

#include "robots.h"

bool Real_time = false;		/* Play in real time? */
bool Jump = false;		/* Jump while running, counting, or waiting */
bool Teleport = false;		/* Teleport automatically when player must */

bool Dead;			/* Player is now dead */
bool Running = false;		/* Currently in the middle of a run */
bool Waiting;			/* Player is waiting for end */
bool Newscore;			/* There was a new score added */
bool Was_bonus = false;		/* Was a bonus last level */
bool Full_clear = true;		/* Lots of junk for init_field to clear */

#ifdef FANCY
bool Pattern_roll = false;	/* Auto play for YHBJNLUK pattern */
bool Stand_still = false;	/* Auto play for standing still pattern */
#endif

char Cnt_move;			/* Command which has preceded the count */
char Run_ch;			/* Character for the direction we are running */

char Field[Y_FIELDSIZE][X_FIELDSIZE];	/* the playing field itself */

char *Next_move;		/* Next move to be used in the pattern */
const char *Move_list = "YHBJNLUK";/* List of moves in the pattern */

int Count = 0;			/* Command count */
int Level;			/* Current level */
int Num_robots;			/* Number of robots left */
int Num_scores;			/* Number of scores posted */
int Start_level = 1;		/* Level on which to start */
int Wait_bonus;			/* bonus for waiting */

int Score;			/* Current score */

COORD Max;			/* Max area robots take up */
COORD Min;			/* Min area robots take up */
COORD My_pos;			/* Player's current position */
COORD Robots[MAXROBOTS];	/* Robots' current positions */

jmp_buf	End_move;		/* Jump to on Real_time */
