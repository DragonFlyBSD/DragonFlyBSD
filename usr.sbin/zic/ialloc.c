#ifndef lint
#ifndef NOID
static char	elsieid[] = "@(#)ialloc.c	8.29";
#endif /* !defined NOID */
#endif /* !defined lint */

/*
 * @(#)ialloc.c	8.29
 * $FreeBSD: src/usr.sbin/zic/ialloc.c,v 1.5 1999/08/28 01:21:18 peter Exp $
 * $DragonFly: src/usr.sbin/zic/ialloc.c,v 1.4 2004/12/18 22:48:15 swildner Exp $
 */
/*LINTLIBRARY*/

#include "private.h"

#define nonzero(n)	(((n) == 0) ? 1 : (n))

char *
imalloc(const int n)
{
	return malloc((size_t) nonzero(n));
}

char *
icalloc(int nelem, int elsize)
{
	if (nelem == 0 || elsize == 0)
		nelem = elsize = 1;
	return calloc((size_t) nelem, (size_t) elsize);
}

void *
irealloc(void *const pointer, const int size)
{
	if (pointer == NULL)
		return imalloc(size);
	return realloc((void *) pointer, (size_t) nonzero(size));
}

char *
icatalloc(char *const old, const char *new)
{
	char *result;
	int oldsize, newsize;

	newsize = (new == NULL) ? 0 : strlen(new);
	if (old == NULL)
		oldsize = 0;
	else if (newsize == 0)
		return old;
	else	oldsize = strlen(old);
	if ((result = irealloc(old, oldsize + newsize + 1)) != NULL)
		if (new != NULL)
			strcpy(result + oldsize, new);
	return result;
}

char *
icpyalloc(const char *string)
{
	return icatalloc((char *) NULL, string);
}

void
ifree(char * const p)
{
	if (p != NULL)
		free(p);
}

void
icfree(char * const p)
{
	if (p != NULL)
		free(p);
}
