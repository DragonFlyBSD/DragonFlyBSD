# $FreeBSD: src/gnu/usr.bin/cc/cccp/Makefile,v 1.24 2002/05/13 03:27:02 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/cccp/Attic/Makefile,v 1.2 2004/01/17 23:05:20 drhodus Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}

PROG=	cpp0
SRCS=	cppmain.c c-parse+%DIKED.c c-lang.c c-decl.c

BINDIR=	/usr/libexec/gcc3
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
