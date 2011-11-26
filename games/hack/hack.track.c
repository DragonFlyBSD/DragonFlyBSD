/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.track.c - version 1.0.2 */
/* $FreeBSD: src/games/hack/hack.track.c,v 1.4 1999/11/16 10:26:38 marcel Exp $ */
/* $DragonFly: src/games/hack/hack.track.c,v 1.3 2006/08/21 19:45:32 pavalos Exp $ */

#include "hack.h"

#define	UTSZ	50

coord utrack[UTSZ];
int utcnt = 0;
int utpnt = 0;

void
initrack(void)
{
	utcnt = utpnt = 0;
}

/* add to track */
void
settrack(void)
{
	if (utcnt < UTSZ)
		utcnt++;
	if (utpnt == UTSZ)
		utpnt = 0;
	utrack[utpnt].x = u.ux;
	utrack[utpnt].y = u.uy;
	utpnt++;
}

coord *
gettrack(int x, int y)
{
	int i, cnt, dst;
	coord tc;

	cnt = utcnt;
	for (i = utpnt - 1; cnt--; i--) {
		if (i == -1)
			i = UTSZ - 1;
		tc = utrack[i];
		dst = (x - tc.x) * (x - tc.x) + (y - tc.y) * (y - tc.y);
		if (dst < 3)
			return (dst ? &(utrack[i]) : 0);
	}
	return (0);
}
