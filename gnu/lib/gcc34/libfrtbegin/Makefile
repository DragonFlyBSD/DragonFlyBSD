# $DragonFly: src/gnu/lib/gcc34/libfrtbegin/Attic/Makefile,v 1.1 2005/12/21 17:07:33 joerg Exp $

.include "../Makefile.inc"

LIB=	frtbegin
SHLIB_MAJOR=	1

SRCDIR=	${GCCDIR}/libf2c

.PATH: ${SRCDIR}/libF77

SRCS=	mainF.c

CFLAGS+=	-I${SRCDIR}/libF77

mainF.c: ${SRCDIR}/libF77/main.c
	cp ${SRCDIR}/libF77/main.c ${.TARGET}

.include <bsd.lib.mk>
