#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.bin/rlogin/Makefile,v 1.21.2.2 2002/07/19 18:03:41 ru Exp $
# $DragonFly: src/usr.bin/rlogin/Makefile,v 1.4 2004/11/17 19:16:06 dillon Exp $

PROG=	rlogin
SRCS=	rlogin.c
CFLAGS+=-Wall

BINMODE=4555
.if !defined(NOFSCHG)
INSTALLFLAGS=-fschg
.endif

.include <bsd.prog.mk>
