# From:	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.bin/login/Makefile,v 1.31.2.2 2001/07/16 07:02:39 markm Exp $
# $DragonFly: src/usr.bin/login/Makefile,v 1.4 2007/08/27 16:50:56 pavalos Exp $

PROG=	login
SRCS=	login.c login_fbtab.c
WARNS?=	2

CFLAGS+=-DLOGALL

DPADD=	${LIBUTIL} ${LIBPAM}
LDADD=	-lutil ${MINUSLPAM}
LDFLAGS+= ${LDFLAGSPAM}

BINOWN=	root
BINMODE=4555
.if !defined(NOFSCHG)
INSTALLFLAGS=-fschg
.endif

.include <bsd.prog.mk>
