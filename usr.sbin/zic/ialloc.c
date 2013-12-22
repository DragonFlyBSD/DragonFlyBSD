/*
** This file is in the public domain, so clarified as of
** 2006-07-17 by Arthur David Olson.
*/

/*
 * $FreeBSD: src/usr.sbin/zic/ialloc.c,v 1.5 1999/08/28 01:21:18 peter Exp $
 */
/*LINTLIBRARY*/

#include "private.h"

char *
icatalloc(char *const old, const char * const new)
{
	char *result;
	int oldsize, newsize;

	newsize = (new == NULL) ? 0 : strlen(new);
	if (old == NULL)
		oldsize = 0;
	else if (newsize == 0)
		return old;
	else	oldsize = strlen(old);
	if ((result = realloc(old, oldsize + newsize + 1)) != NULL)
		if (new != NULL)
			strcpy(result + oldsize, new);
	return result;
}

char *
icpyalloc(const char * const string)
{
	return icatalloc(NULL, string);
}
