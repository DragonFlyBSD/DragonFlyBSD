# $FreeBSD: src/gnu/usr.bin/cc/c++/Makefile,v 1.19 2002/06/04 19:45:08 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/c++/Attic/Makefile,v 1.4 2004/01/24 20:03:27 joerg Exp $

.include "../Makefile.inc"
.include "../Makefile.fe"

.PATH: ${GCCDIR}/cp ${GCCDIR}

PROG=	c++
SRCS=	gcc.c g++spec.c
LINKS=	${BINDIR}/c++ ${BINDIR}/g++
LINKS+=	${BINDIR}/c++ ${BINDIR}/CC
NOMAN=	1

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"
CFLAGS+= -DCCVER=\"$(MY_CCVER)\"

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

.include <bsd.prog.mk>
