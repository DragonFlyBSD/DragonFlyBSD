/*
 * quit.c
 *
 * By Ross Ridge
 * Public Domain
 * 92/02/01 07:30:14
 *
 * quit with a diagnostic message printed on stderr
 *
 * $DragonFly: src/usr.bin/tconv/Attic/quit.c,v 1.2 2003/10/04 20:36:52 hmp Exp $
 */

#define NOTLIB
#include "defs.h"

#ifdef USE_SCCS_IDS
static const char SCCSid[] = "@(#) mytinfo quit.c 3.2 92/02/01 public domain, By Ross Ridge";
#endif

char *prg_name;

#if defined(USE_PROTOTYPES) && !defined(lint)
void (*cleanup)(int);
#else
void (*cleanup)();
#endif

/* PRINTFLIKE2 */
noreturn
void
quit(int e, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	(*cleanup)(e);

	if (e != 0)
		fprintf(stderr, "%s: ", prg_name);
#ifdef USE_DOPRNT
	_doprnt(fmt, ap, stderr);
#else
	vfprintf(stderr, fmt, ap);
#endif
	putc('\n', stderr);
	if (e > 0 && e < sys_nerr) {
		fprintf(stderr, "%d - %s\n", e, sys_errlist[e]);
	}
	fflush(stderr);
	exit(e);
}
