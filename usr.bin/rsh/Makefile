#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.bin/rsh/Makefile,v 1.17.2.1 2002/07/17 19:08:22 ru Exp $
# $DragonFly: src/usr.bin/rsh/Makefile,v 1.5 2005/01/01 22:00:15 cpressey Exp $

PROG=	rsh
SRCS=	rsh.c
CFLAGS+=-I${.CURDIR}/../../libexec/rlogind
WARNS?=	6

BINMODE=4555
.if !defined(NOFSCHG)
INSTALLFLAGS=-fschg
.endif

.include <bsd.prog.mk>
