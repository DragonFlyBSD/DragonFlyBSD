# $FreeBSD: src/gnu/usr.bin/cc/f77/Makefile,v 1.21 2004/01/05 20:23:16 kan Exp $
# $DragonFly: src/gnu/usr.bin/cc3/f77/Attic/Makefile,v 1.4 2004/01/24 20:03:28 joerg Exp $

.include "../Makefile.inc"
.include "../Makefile.fe"

.PATH: ${GCCDIR}/f ${GCCDIR}

PROG=	f77
SRCS=	gcc.c g77spec.c

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"
CFLAGS+= -DCCVER=\"$(MY_CCVER)\"
CFLAGS+= -DFORTRAN_INIT=\"-lg2c\" -DFORTRAN_INIT_PROFILE=\"-lg2c_p\"

DPADD=	${LIBCC_INT}
LDADD=	${LIBCC_INT}

CLEANFILES= f77.1

f77.1: g77.1
	cat ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>
