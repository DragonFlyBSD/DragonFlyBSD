#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.bin/rsh/Makefile,v 1.17.2.1 2002/07/17 19:08:22 ru Exp $
# $DragonFly: src/usr.bin/rsh/Makefile,v 1.3 2003/08/05 07:45:43 asmodai Exp $

PROG=	rsh
SRCS=	rsh.c
CFLAGS+=-Wall
CFLAGS+=-I${.CURDIR}/../../libexec/rlogind

BINMODE=4555
INSTALLFLAGS=-fschg

.include <bsd.prog.mk>
