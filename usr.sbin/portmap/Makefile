#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/portmap/Makefile,v 1.7.2.1 2001/04/25 12:10:33 ru Exp $
# $DragonFly: src/usr.sbin/portmap/Makefile,v 1.2 2003/06/17 04:30:00 dillon Exp $

PROG=	portmap
MAN=	portmap.8
SRCS=	portmap.c from_local.c pmap_check.c
SUBDIR= pmap_set pmap_dump

CFLAGS+=-DCHECK_PORT -DHOSTS_ACCESS 
DPADD=	${LIBWRAP}
LDADD=	-lwrap

.include <bsd.prog.mk>
