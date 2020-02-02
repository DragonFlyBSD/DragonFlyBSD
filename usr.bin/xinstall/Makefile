#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/xinstall/Makefile,v 1.11.2.4 2001/09/24 15:44:05 ru Exp $

PROG=		xinstall
PROGNAME=	install
SRCS=		xinstall.c
MAN=		install.1

.if defined(BOOTSTRAPPING)
CFLAGS+=	-DBOOTSTRAPPING
.else
.PATH:          ${.CURDIR}/../../usr.sbin/mtree
SRCS+=		getid.c
CFLAGS+=        -DHAVE_NETDB_H -I${.CURDIR}/../../usr.sbin/mtree
.endif

.include <bsd.prog.mk>
