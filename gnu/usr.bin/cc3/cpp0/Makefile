# $FreeBSD: src/gnu/usr.bin/cc/cpp0/Makefile,v 1.5 2002/05/13 03:27:03 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/cpp0/Attic/Makefile,v 1.2 2004/02/02 05:43:12 dillon Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}

PROG=	cpp0
SRCS=	cppmain.c c-parse+%DIKED.c c-lang.c c-decl.c

NOMAN=	1
NOSHARED?=yes

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

#-----------------------------------------------------------------------
# C parser

c-parse+%DIKED.c: c-parse.c
	sed -e "s/malloc/xmalloc/g" \
	    -e "s/realloc/xrealloc/g" \
	    ${.ALLSRC} > ${.TARGET}

c-parse.y: c-parse.in
	sed -e "/^ifobjc$$/,/^end ifobjc$$/d" \
	    -e "/^ifc$$/d" \
	    -e "/^end ifc$$/d" \
	    ${.ALLSRC} > ${.TARGET}

CLEANFILES=	c-parse+%DIKED.c c-parse.c c-parse.y
CLEANFILES+=	y.tab.h # we don't use it, but the system YACC rules are naive

#-----------------------------------------------------------------------

.include <bsd.prog.mk>
