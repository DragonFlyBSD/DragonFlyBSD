/* $FreeBSD: src/usr.sbin/pkg_install/sign/stand.h,v 1.1.2.1 2001/03/05 03:43:53 wes Exp $ */
/* $DragonFly: src/usr.sbin/pkg_install/sign/Attic/stand.h,v 1.4 2003/11/06 19:46:42 eirikn Exp $ */
/* $OpenBSD: stand.h,v 1.2 1999/10/04 21:46:30 espie Exp $ */

/* provided to cater for BSD idiosyncrasies */

#if (defined(__unix__) || defined(unix)) && !defined(USG)
#include <sys/param.h>
#endif

#if defined(BSD4_4)
#include <err.h>
#else
extern void set_program_name(const char * name);
extern void warn(const char *fmt, ...);
extern void warnx(const char *fmt, ...);
#endif

#ifndef __GNUC__
#define __attribute__(x)
#endif
