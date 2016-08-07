#	from: @(#)Makefile	5.8 (Berkeley) 7/28/90
# $FreeBSD: src/usr.sbin/ypbind/Makefile,v 1.6.2.1 2001/04/25 12:11:13 ru Exp $
# $DragonFly: src/usr.sbin/ypbind/Makefile,v 1.2 2003/06/17 04:30:04 dillon Exp $

SRCS=	ypbind.c yp_ping.c
PROG=	ypbind
MAN=	ypbind.8
CFLAGS+=-DDAEMON
WARNS?=	2
NO_STRICT_ALIASING=

.include <bsd.prog.mk>
