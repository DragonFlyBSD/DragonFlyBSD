# $FreeBSD: src/gnu/usr.bin/cc/cc1/Makefile,v 1.29 2003/07/11 05:37:23 kan Exp $
# $DragonFly: src/gnu/usr.bin/cc3/cc1/Attic/Makefile,v 1.5 2004/02/03 03:47:11 dillon Exp $

.include "../Makefile.inc"

CFLAGS+=        -I${.CURDIR}/../config

.PATH: ${GCCDIR}/gcc ${GCCDIR}/libiberty
 
PROG=	cc1
SRCS=	main.c c-parse+%DIKED.c c-lang.c c-decl.c c-opts.c
SRCS+=	xmalloc.c xexit.c
NOMAN=	1
NOSHARED?=yes

CFLAGS+= -I.

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

.include <bsd.prog.mk>
