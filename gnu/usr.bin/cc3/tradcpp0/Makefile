# $FreeBSD: src/gnu/usr.bin/cc/tradcpp0/Makefile,v 1.6 2002/05/13 03:27:03 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/tradcpp0/Attic/Makefile,v 1.1 2004/01/17 22:40:16 drhodus Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}

PROG=	tradcpp0
SRCS=	tradcpp.c tradcif+%DIKED.c
BINDIR=	/usr/bin
NOMAN=	1

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

tradcif+%DIKED.c: tradcif.c
	sed -e "s/malloc/xmalloc/g" \
	    -e "s/realloc/xrealloc/g" \
	    ${.ALLSRC} >${.TARGET}

CLEANFILES=	tradcif+%DIKED.c tradcif.c
CLEANFILES+=	y.tab.h # we don't use it, but the system YACC rules are naive

.include <bsd.prog.mk>
