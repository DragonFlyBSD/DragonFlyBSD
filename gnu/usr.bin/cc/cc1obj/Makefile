# $FreeBSD: src/gnu/usr.bin/cc/cc1obj/Makefile,v 1.11.2.2 2002/06/20 23:13:31 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc/cc1obj/Attic/Makefile,v 1.5 2004/02/02 05:43:11 dillon Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}/objc ${GCCDIR}

PROG=	cc1obj
SRCS=	c-parse.y objc-act.c
# Ugh, compiled twice...
SRCS+=	c-decl.c c-lex.c
NOMAN=	1
NOSHARED=yes

CFLAGS+= -I${GCCDIR}/objc -I.

DPADD+=	${LIBCC_INT} 
LDADD+=	${LIBCC_INT}

#-----------------------------------------------------------------------
# objc parser

c-parse.y: c-parse.in
	sed -e "/^ifc$$/,/^end ifc$$/d" \
	    -e "/^ifobjc$$/d" \
	    -e "/^end ifobjc$$/d" \
	    ${.ALLSRC} > ${.TARGET}

CLEANFILES+=	c-parse.y

#-----------------------------------------------------------------------

.include <bsd.prog.mk>
