/*
 * $OpenBSD: stand.h,v 1.2 1999/10/04 21:46:30 espie Exp $
 * $FreeBSD: src/usr.sbin/pkg_install/sign/stand.h,v 1.2 2004/06/29 19:06:42 eik Exp $
 * $DragonFly: src/usr.sbin/pkg_install/sign/Attic/stand.h,v 1.5 2004/07/30 04:46:14 dillon Exp $
 */

/* provided to cater for BSD idiosyncrasies */

#if (defined(__unix__) || defined(unix)) && !defined(USG)
#include <sys/param.h>
#endif

#if defined(BSD4_4)
#include <err.h>
#else
extern void warn (const char *fmt, ...);
extern void warnx (const char *fmt, ...);
#endif
extern void set_program_name (const char * name);

#ifndef __GNUC__
#define __attribute__(x)
#endif
