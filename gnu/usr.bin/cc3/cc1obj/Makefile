# $FreeBSD: src/gnu/usr.bin/cc/cc1obj/Makefile,v 1.22 2002/05/13 03:27:02 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/cc1obj/Attic/Makefile,v 1.4 2004/02/02 23:35:10 dillon Exp $

.include "../Makefile.inc"

CFLAGS+=        -I${.CURDIR}/../config
CFLAGS+=	-I${GCCDIR}/gcc/objc

.PATH: ${GCCDIR}/gcc/objc ${GCCDIR}/gcc

PROG=	cc1obj
SRCS=	objc-parse+DIKED.c objc-act.c objc-lang.c main.c c-decl.c
NOMAN=	1
NOSHARED?=yes

CFLAGS+= -I${GCCDIR}/objc -I.

DPADD+=	${LIBCC_INT} 
LDADD+=	${LIBCC_INT}

#-----------------------------------------------------------------------
# objc parser
objc-parse+DIKED.c: objc-parse.c
	sed -e "s/malloc/xmalloc/g" \
	    -e "s/realloc/xrealloc/g" \
	    ${.ALLSRC} > ${.TARGET}

objc-parse.y: c-parse.in
	sed -e "/^ifc$$/,/^end ifc$$/d" \
	    -e "/^ifobjc$$/d" \
	    -e "/^end ifobjc$$/d" \
	    ${.ALLSRC} > ${.TARGET}

CLEANFILES=	objc-parse+DIKED.c objc-parse.c objc-parse.y
CLEANFILES+=	y.tab.h # we don't use it, but the system YACC rules are naive

#-----------------------------------------------------------------------

.include <bsd.prog.mk>
