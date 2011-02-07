/* alloc.c - version 1.0.2 */
/* $FreeBSD: src/games/hack/alloc.c,v 1.4 1999/11/16 02:57:01 billf Exp $ */
/* $DragonFly: src/games/hack/alloc.c,v 1.4 2006/08/21 19:45:32 pavalos Exp $ */

#include "hack.h"

#ifdef LINT

/*
   a ridiculous definition, suppressing
	"possible pointer alignment problem" for (long *) malloc()
	"enlarg defined but never used"
	"ftell defined (in <stdio.h>) but never used"
   from lint
*/
long *
alloc(size_t n)
{
	long dummy = ftell(stderr);

	if (n)
		dummy = 0;	/* make sure arg is used */
	return (&dummy);
}

#else

void *
alloc(size_t lth)
{
	void *ptr;

	if ((ptr = malloc(lth)) == NULL)
		panic("Cannot get %zd bytes", lth);
	return (ptr);
}

#endif /* LINT */
