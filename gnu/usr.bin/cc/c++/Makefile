# $FreeBSD: src/gnu/usr.bin/cc/c++/Makefile,v 1.16.2.3 2002/11/15 18:18:38 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc/c++/Attic/Makefile,v 1.5 2004/01/24 20:03:27 joerg Exp $

.include "../Makefile.inc"

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
