/*
 * MISC.C
 *
 * $DragonFly: src/bin/cpdup/misc.c,v 1.2 2003/12/01 06:07:16 dillon Exp $
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
    char *ptr = NULL;
    va_list va;

    va_start(va, ctl);
    if (vasprintf(&ptr, ctl, va) < 0)
	fatal("malloc failed");
    va_end(va);
    assert(ptr != NULL);
    return(ptr);
}

void
fatal(const char *ctl, ...)
{
    va_list va;

    if (ctl == NULL) {
	puts("cpdup [<options>] src [dest]");
	puts("    -v[vv]      verbose level (-vv is typical)\n"
	     "    -I          display performance summary\n"
	     "    -f          force update even if files look the same\n"
	     "    -i0         do NOT confirm when removing something\n"
	     "    -s0         disable safeties - allow files to overwrite directories\n"
	     "    -q          quiet operation\n"
	     "    -o          do not remove any files, just overwrite/add\n"
	     "    -m          maintain/generate MD5 checkfile on source,\n"
	     "                and compare with (optional) destination,\n"
	     "                copying if the compare fails\n"
	     "    -M file     -m+specify MD5 checkfile, else .MD5_CHECKSUMS\n"
	     "                copy if md5 check fails\n"
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

