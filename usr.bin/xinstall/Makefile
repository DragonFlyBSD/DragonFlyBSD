#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/xinstall/Makefile,v 1.11.2.4 2001/09/24 15:44:05 ru Exp $
# $DragonFly: src/usr.bin/xinstall/Makefile,v 1.2 2003/06/17 04:29:34 dillon Exp $

PROG=		xinstall
PROGNAME=	install
SRCS=		xinstall.c
MAN=		install.1

.if defined(BOOTSTRAPPING)
.PATH: ${.CURDIR}/../../lib/libc/gen
SRCS+=		strtofflags.c
.endif

.include <bsd.prog.mk>
