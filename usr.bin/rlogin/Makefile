#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.bin/rlogin/Makefile,v 1.21.2.2 2002/07/19 18:03:41 ru Exp $
# $DragonFly: src/usr.bin/rlogin/Makefile,v 1.5 2005/02/15 01:24:49 cpressey Exp $

PROG=	rlogin
WARNS?=	6
SRCS=	rlogin.c
CFLAGS+=-Wall

BINMODE=4555
.if !defined(NOFSCHG)
INSTALLFLAGS=-fschg
.endif

.include <bsd.prog.mk>
