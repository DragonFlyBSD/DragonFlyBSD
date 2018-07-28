#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/xinstall/Makefile,v 1.11.2.4 2001/09/24 15:44:05 ru Exp $

PROG=		xinstall
PROGNAME=	install
SRCS=		xinstall.c getid.c
MAN=		install.1

.PATH:          ${.CURDIR}/../../usr.sbin/mtree
CFLAGS+=        -I${.CURDIR}/../../usr.sbin/mtree

.include <bsd.prog.mk>
