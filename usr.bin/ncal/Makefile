# $FreeBSD: src/usr.bin/ncal/Makefile,v 1.2 1999/08/28 01:04:25 peter Exp $
# $DragonFly: src/usr.bin/ncal/Makefile,v 1.4 2007/08/27 16:50:56 pavalos Exp $

PROG= ncal
WARNS?=	1

DPADD+=		${LIBCALENDAR} ${LIBTERMCAP}
LDADD+=		-lcalendar -ltermcap

LINKS=		${BINDIR}/ncal ${BINDIR}/cal
MLINKS=		ncal.1 cal.1

.include <bsd.prog.mk>
