# $FreeBSD: src/gnu/usr.bin/texinfo/texindex/Makefile,v 1.7.2.1 2002/08/07 16:53:39 ru Exp $
# $DragonFly: src/gnu/usr.bin/texinfo/texindex/Makefile,v 1.3 2006/02/11 01:21:27 joerg Exp $

PROG=	texindex

PATCHES=	texindex.c.patch
CONTRIBDIR=	${TXIDIR}/util

DPADD=	${LIBTXI}
LDADD=	${LIBTXI}

.include <bsd.prog.mk>

.PATH:	${TXIDIR}/util ${TXIDIR}/doc
