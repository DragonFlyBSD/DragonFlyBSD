/*
 * $FreeBSD: src/lib/libc/gen/getprogname.c,v 1.1.2.1 2001/06/14 00:06:12 dd Exp $
 * $DragonFly: src/lib/libc/gen/getprogname.c,v 1.3 2005/03/09 18:52:21 joerg Exp $
 */

#include <stdlib.h>

extern const char *__progname;

const char *
getprogname(void)
{
	return (__progname);
}
