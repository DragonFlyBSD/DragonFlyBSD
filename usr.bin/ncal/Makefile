# $FreeBSD: src/usr.bin/ncal/Makefile,v 1.2 1999/08/28 01:04:25 peter Exp $
# $DragonFly: src/usr.bin/ncal/Makefile,v 1.2 2003/06/17 04:29:29 dillon Exp $
PROG= ncal
SRCS= ncal.c

CFLAGS+=	-Wall -Wmissing-prototypes -fstrict-prototypes -ansi -pedantic

DPADD+=		${LIBCALENDAR}
LDADD+=		-lcalendar

LINKS=		${BINDIR}/ncal ${BINDIR}/cal
MLINKS=		ncal.1 cal.1

.include <bsd.prog.mk>
