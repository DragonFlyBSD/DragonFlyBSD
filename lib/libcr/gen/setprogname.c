/*
 * $FreeBSD: src/lib/libc/gen/setprogname.c,v 1.1.2.4 2002/02/11 01:18:35 dd Exp $
 * $DragonFly: src/lib/libcr/gen/Attic/setprogname.c,v 1.2 2003/06/17 04:26:42 dillon Exp $
 */

#include <stdlib.h>
#include <string.h>

extern const char *__progname;

void
setprogname(const char *progname)
{
	const char *p;

	p = strrchr(progname, '/');
	if (p != NULL)
		__progname = p + 1;
	else
		__progname = progname;
}
