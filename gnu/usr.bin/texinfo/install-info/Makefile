# $FreeBSD: src/gnu/usr.bin/texinfo/install-info/Makefile,v 1.11.2.1 2002/08/07 16:53:36 ru Exp $
# $DragonFly: src/gnu/usr.bin/texinfo/install-info/Makefile,v 1.4 2005/08/28 14:19:30 asmodai Exp $

PATCHES!=	echo ${.CURDIR}/patches/*.patch
CONTRIBDIR=	${TXIDIR}/util

PROG=	install-info
SRCS=	install-info.c
SRCS+=	${PATCHES}

DPADD=	${LIBTXI}
LDADD=	${LIBTXI}

.include <bsd.prog.mk>

.PATH:	${TXIDIR}/util ${TXIDIR}/doc
