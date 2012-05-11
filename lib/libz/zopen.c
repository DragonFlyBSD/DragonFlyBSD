/*
 * Public domain stdio wrapper for libz, written by Johan Danielsson.
 *
 * $FreeBSD: src/lib/libz/zopen.c,v 1.2.2.2 2003/02/01 13:33:12 sobomax Exp $
 */

#include <stdio.h>
#include <zlib.h>

FILE *zopen(const char *fname, const char *mode);

/* convert arguments */
static int
xgzread(void *cookie, char *data, int size)
{
    return gzread(cookie, data, size);
}

static int
xgzwrite(void *cookie, const char *data, int size)
{
    return gzwrite(cookie, (void*)data, size);
}

static int
xgzclose(void *cookie)
{
    return gzclose((gzFile) cookie);
}

FILE *
zopen(const char *fname, const char *mode)
{
    gzFile gz = gzopen(fname, mode);
    if(gz == NULL)
	return NULL;

    if(*mode == 'r')
	return (funopen(gz, xgzread, NULL, NULL, xgzclose));
    else
	return (funopen(gz, NULL, xgzwrite, NULL, xgzclose));
}
