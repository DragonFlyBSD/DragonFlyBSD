# $FreeBSD: src/usr.bin/ncal/Makefile,v 1.2 1999/08/28 01:04:25 peter Exp $

PROG= ncal

DPADD+=		${LIBCALENDAR} ${LIBNCURSES}
LDADD+=		-lcalendar -lprivate_ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

LINKS=		${BINDIR}/ncal ${BINDIR}/cal
MLINKS=		ncal.1 cal.1

.include <bsd.prog.mk>
