# $FreeBSD: src/gnu/usr.bin/cc/c++/Makefile,v 1.16.2.3 2002/11/15 18:18:38 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc/c++/Attic/Makefile,v 1.7 2004/06/21 03:48:05 dillon Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}/cp ${GCCDIR}

PROG=	c++
SRCS=	gcc.c g++spec.c
LINKS=	${BINDIR}/c++ ${BINDIR}/g++
LINKS+=	${BINDIR}/c++ ${BINDIR}/CC
NOMAN=	1

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"
CFLAGS+= -DBINUTILS=\"$(MY_BINUTILS)\"

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

.include <bsd.prog.mk>
