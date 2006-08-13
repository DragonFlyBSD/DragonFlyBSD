/*
 * MISC.C
 *
 * $DragonFly: src/bin/cpdup/misc.c,v 1.7 2006/08/13 20:51:40 dillon Exp $
 */

#include "cpdup.h"

void
logstd(const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vprintf(ctl, va);
    va_end(va);
}

void
logerr(const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vfprintf(stderr, ctl, va);
    va_end(va);
}

char *
mprintf(const char *ctl, ...)
{
    char *ptr;
    va_list va;

    ptr = NULL;

    va_start(va, ctl);
    if (vasprintf(&ptr, ctl, va) < 0)
	fatal("malloc failed");
    va_end(va);
    assert(ptr != NULL);
    return(ptr);
}

char *
fextract(FILE *fi, int n, int *pc, int skip)
{
    int i;
    int c;
    int imax;
    char *s;

    i = 0;
    c = *pc;
    imax = (n < 0) ? 64 : n + 1;

    s = malloc(imax);
    if (s == NULL) {
	fprintf(stderr, "out of memory\n");
	exit(EXIT_FAILURE);
    }

    while (c != EOF) {
	if (n == 0 || (n < 0 && (c == ' ' || c == '\n')))
	    break;

	s[i++] = c;
	if (i == imax) {
	    imax += 64;
	    s = realloc(s, imax);
    	    if (s == NULL) {
                fprintf(stderr, "out of memory\n");
  	        exit(EXIT_FAILURE);
 	    }
	}
	if (n > 0)
	    --n;
	c = getc(fi);
    }
    if (c == skip && skip != EOF)
	c = getc(fi);
    *pc = c;
    s[i] = 0;
    return(s);
}

void
fatal(const char *ctl, ...)
{
    va_list va;

    if (ctl == NULL) {
	puts("cpdup [<options>] src [dest]");
	puts("    -v[vv]      verbose level (-vv is typical)\n"
	     "    -u          use unbuffered output for -v[vv]\n"
	     "    -I          display performance summary\n"
	     "    -f          force update even if files look the same\n"
	     "    -i0         do NOT confirm when removing something\n"
	     "    -s0         disable safeties - allow files to overwrite directories\n"
	     "    -q          quiet operation\n"
	     "    -o          do not remove any files, just overwrite/add\n"
	);
	puts(
#ifndef NOMD5
	     "    -m          maintain/generate MD5 checkfile on source,\n"
	     "                and compare with (optional) destination,\n"
	     "                copying if the compare fails\n"
	     "    -M file     -m+specify MD5 checkfile, else .MD5_CHECKSUMS\n"
	     "                copy if md5 check fails\n"
#endif
	     "    -x          use .cpignore as exclusion file\n"
	     "    -X file     specify exclusion file\n"
	     " Version 1.06 by Matt Dillon and Dima Ruban\n"
	);
	exit(0);
    } else {
	va_start(va, ctl);
	vprintf(ctl, va);
	va_end(va);
	puts("");
	exit(1);
    }
}
