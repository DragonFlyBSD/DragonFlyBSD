/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.Decl.c - version 1.0.3 */

#include "hack.h"
char nul[40];			/* contains zeros */
char plname[PL_NSIZ];		/* player name */
char lock[PL_NSIZ + 4] = "1lock";	/* long enough for login name .99 */

boolean in_mklev, restoring;

struct rm levl[COLNO][ROWNO];	/* level map */
#ifndef QUEST
struct mkroom rooms[MAXNROFROOMS + 1];
coord doors[DOORMAX];
#endif /* QUEST */
struct monst *fmon = NULL;
struct trap *ftrap = NULL;
struct gold *fgold = NULL;
struct obj *fobj = NULL, *fcobj = NULL, *invent = NULL, *uwep = NULL, *uarm = NULL,
	*uarm2 = NULL, *uarmh = NULL, *uarms = NULL, *uarmg = NULL, *uright = NULL,
	*uleft = NULL, *uchain = NULL, *uball = NULL;
struct flag flags;
struct you u;
struct monst youmonst;	/* dummy; used as return value for boomhit */

xchar dlevel = 1;
xchar xupstair, yupstair, xdnstair, ydnstair;
const char *save_cm, *killer, *nomovemsg;

long moves = 1;
long wailmsg = 0;

int multi = 0;
char genocided[60];
char fut_geno[60];

xchar curx, cury;
xchar seelx, seehx, seely, seehy;	/* corners of lit room */

coord bhitpos;

char quitchars[] = " \r\n\033";
