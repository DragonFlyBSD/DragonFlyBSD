/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* def.wseg.h - version 1.0.2 */
/* $DragonFly: src/games/hack/def.wseg.h,v 1.2 2004/11/06 12:29:17 eirikn Exp $ */

#ifndef NOWORM
/* worm structure */
struct wseg {
	struct wseg *nseg;
	xchar wx,wy;
	unsigned wdispl:1;
};

#define newseg()	(struct wseg *) alloc(sizeof(struct wseg))
#endif /* NOWORM */
