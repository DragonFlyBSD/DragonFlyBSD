# $FreeBSD: src/usr.bin/ncal/Makefile,v 1.2 1999/08/28 01:04:25 peter Exp $
# $DragonFly: src/usr.bin/ncal/Makefile,v 1.3 2004/01/24 22:29:20 joerg Exp $
PROG= ncal
SRCS= ncal.c

CFLAGS+=	-Wall -Wmissing-prototypes -ansi -pedantic

DPADD+=		${LIBCALENDAR}
LDADD+=		-lcalendar

LINKS=		${BINDIR}/ncal ${BINDIR}/cal
MLINKS=		ncal.1 cal.1

.include <bsd.prog.mk>
