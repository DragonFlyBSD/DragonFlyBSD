# $FreeBSD: src/gnu/usr.bin/texinfo/install-info/Makefile,v 1.11.2.1 2002/08/07 16:53:36 ru Exp $
# $DragonFly: src/gnu/usr.bin/texinfo/install-info/Makefile,v 1.3 2005/08/28 12:25:04 asmodai Exp $

PATCHES!=	echo ${.CURDIR}/patches/*.patch
CONTRIBDIR=	${TXIDIR}/util

PROG=	install-info
SRCS+=	${PATCHES}

DPADD=	${LIBTXI}
LDADD=	${LIBTXI}

.include <bsd.prog.mk>

.PATH:	${TXIDIR}/util ${TXIDIR}/doc
