# $FreeBSD: src/gnu/usr.bin/cc/f77/Makefile,v 1.21 2004/01/05 20:23:16 kan Exp $
# $DragonFly: src/gnu/usr.bin/cc3/f77/Attic/Makefile,v 1.8 2004/06/21 03:48:06 dillon Exp $

.include "../Makefile.inc"
.include "../Makefile.fe"

CFLAGS+=        -I${.CURDIR}/../config

.PATH: ${GCCDIR}/gcc/f ${GCCDIR}/gcc ${GCCDIR}/libiberty

PROG=	f77
SRCS=	gcc_local.c g77spec.c
SRCS+=  make-relative-prefix.c lrealpath.c xmalloc.c xexit.c

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"
CFLAGS+= -DFORTRAN_INIT=\"-lg2c\" -DFORTRAN_INIT_PROFILE=\"-lg2c_p\"

DPADD=	${LIBCC_INT}
LDADD=	${LIBCC_INT}

CLEANFILES+= f77.1

f77.1: g77.1
	cat ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>
