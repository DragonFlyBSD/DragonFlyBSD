# $FreeBSD: src/gnu/usr.bin/cc/c++/Makefile,v 1.19 2002/06/04 19:45:08 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/c++/Attic/Makefile,v 1.5 2004/02/02 23:35:10 dillon Exp $

.include "../Makefile.inc"
.include "../Makefile.fe"

CFLAGS+=        -I${.CURDIR}/../config

.PATH: ${GCCDIR}/gcc/cp ${GCCDIR}/gcc ${GCCDIR}/libiberty

PROG=	c++
SRCS=	gcc_local.c g++spec.c
SRCS+=  make-relative-prefix.c lrealpath.c
LINKS=	${BINDIR}/c++ ${BINDIR}/g++
LINKS+=	${BINDIR}/c++ ${BINDIR}/CC
NOMAN=	1

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"
CFLAGS+= -DCCVER=\"$(MY_CCVER)\"

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

.include <bsd.prog.mk>
