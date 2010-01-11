/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* def.gold.h - version 1.0.2 */
/* $DragonFly: src/games/hack/def.gold.h,v 1.2 2006/08/21 19:45:32 pavalos Exp $ */

struct gold {
	struct gold *ngold;
	xchar gx,gy;
	long amount;
};

extern struct gold *fgold;
#define	newgold()	alloc(sizeof(struct gold))
