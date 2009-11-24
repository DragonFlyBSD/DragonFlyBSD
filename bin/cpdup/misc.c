/*
 * MISC.C
 *
 * $DragonFly: src/bin/cpdup/misc.c,v 1.16 2008/09/15 20:13:16 thomas Exp $
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

#ifdef DEBUG_MALLOC

#undef malloc
#undef free

struct malloc_info {
	struct malloc_info *next;
	struct malloc_info *prev;
	const char *file;
	int magic;
	int line;
};

struct malloc_info DummyInfo = { &DummyInfo, &DummyInfo, NULL, 0, 0 };
struct malloc_info *InfoList = &DummyInfo;

void *
debug_malloc(size_t bytes, const char *file, int line)
{
	struct malloc_info *info = malloc(sizeof(*info) + bytes);

	info->magic = 0x5513A4C2;
	info->file = file;
	info->line = line;

	info->next = InfoList;
	info->prev = InfoList->prev;
	info->next->prev = info;
	info->prev->next = info;
	return(info + 1);
}

void
debug_free(void *ptr)
{
	struct malloc_info *info = (struct malloc_info *)ptr - 1;
	struct malloc_info *scan;
	static int report;

	for (scan = DummyInfo.next; scan != &DummyInfo; scan = scan->next) {
		if (info == scan) {
			assert(info->magic == 0x5513A4C2);
			info->magic = 0;
			info->next->prev = info->prev;
			info->prev->next = info->next;
			free(info);
			break;
		}
	}
	if (scan == &DummyInfo)
		free(ptr);

	if ((++report & 65535) == 0) {
		printf("--- report\n");
		for (scan = DummyInfo.next; scan != &DummyInfo; scan = scan->next) {
			printf("%-15s %d\n", scan->file, scan->line);
		}
	}
}

#endif

void
fatal(const char *ctl, ...)
{
    va_list va;

    if (ctl == NULL) {
	puts("cpdup [<options>] src [dest]");
	puts("    -C          request compressed ssh link if remote operation\n"
	     "    -v[vv]      verbose level (-vv is typical)\n"
	     "    -u          use unbuffered output for -v[vv]\n"
	     "    -I          display performance summary\n"
	     "    -f          force update even if files look the same\n"
	     "    -F<ssh_opt> Add <ssh_opt> to options passed to ssh\n"
	     "    -i0         do NOT confirm when removing something\n"
	     "    -l          force line-buffered stdout/stderr\n"
	     "    -pN         N parallel transactions for for remote\n"
	     "                source or destination\n"
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
	     "    -H path     hardlink from path to target instead of copying\n"
	     "                source to target, if source matches path.\n"
	     "    -V          verify file contents even if they appear\n"
	     "                to be the same.\n"
#endif
	     "    -x          use .cpignore as exclusion file\n"
	     "    -X file     specify exclusion file\n"
	     " Version 1.15 by Matt Dillon and Dima Ruban\n"
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
