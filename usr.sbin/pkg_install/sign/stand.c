/*
 * $FreeBSD: src/usr.sbin/pkg_install/sign/stand.c,v 1.2 2002/04/01 09:39:07 obrien Exp $
 * $DragonFly: src/usr.sbin/pkg_install/sign/Attic/stand.c,v 1.4 2004/12/18 22:48:04 swildner Exp $
 */

#include "stand.h"

#ifdef BSD4_4
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>

/* shortened version of warn */
static const char *program_name;

void 
set_program_name(n)
	const char *n;
{
	if ((program_name = strrchr(n, '/')) != NULL)
		program_name++;
	else
		program_name = n;
}

void 
warn(const char *fmt, ...)
{
	va_list ap;
	int interrno;

	va_start(ap, fmt);

	interrno = errno;
	fprintf(stderr, "%s: ", program_name);
	if (fmt != NULL) {
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, ": ");
	}
	fprintf(stderr, "%s\n", strerror(interrno));

	va_end(ap);
}

void 
warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", program_name);
	if (fmt != NULL) 
		vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

#endif
