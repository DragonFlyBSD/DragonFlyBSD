/*
 * $FreeBSD: src/lib/libc/gen/getprogname.c,v 1.1.2.1 2001/06/14 00:06:12 dd Exp $
 * $DragonFly: src/lib/libcr/gen/Attic/getprogname.c,v 1.2 2003/06/17 04:26:42 dillon Exp $
 */

extern const char *__progname;

const char *
getprogname(void)
{

	return (__progname);
}
