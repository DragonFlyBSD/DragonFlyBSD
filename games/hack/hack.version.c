/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.version.c - version 1.0.3 */
/* $FreeBSD: src/games/hack/hack.version.c,v 1.3 1999/08/27 23:29:05 peter Exp $ */
/* $DragonFly: src/games/hack/hack.version.c,v 1.2 2003/06/17 04:25:24 dillon Exp $ */

#include	"date.h"

doversion(){
	pline("%s 1.0.3 - last edit %s.", (
#ifdef QUEST
		"Quest"
#else
		"Hack"
#endif QUEST
		), datestring);
	return(0);
}
