/* nap.c		 Larn is copyrighted 1986 by Noah Morgan. */
/* $FreeBSD: src/games/larn/nap.c,v 1.4 1999/11/16 02:57:23 billf Exp $ */
/* $DragonFly: src/games/larn/nap.c,v 1.4 2006/08/26 17:05:05 pavalos Exp $ */
#include "header.h"
/*
 *	routine to take a nap for n milliseconds
 */
void
nap(int x)
{
	if (x <= 0)	/* eliminate chance for infinite loop */
		return;
	lflush();
	usleep(x * 1000);
}
