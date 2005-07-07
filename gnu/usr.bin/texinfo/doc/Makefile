# $FreeBSD: src/gnu/usr.bin/texinfo/doc/Makefile,v 1.12 2000/01/17 10:59:24 ru Exp $
# $DragonFly: src/gnu/usr.bin/texinfo/doc/Makefile,v 1.3 2005/07/07 11:49:55 corecode Exp $

INFO=	info info-stnd texinfo
SRCDIR=	${TXIDIR}/doc

CLEANFILES=	texinfo.texi

texinfo.texi: texinfo.txi
	${LN} -fs ${.ALLSRC} ${.TARGET}

.include <bsd.info.mk>
