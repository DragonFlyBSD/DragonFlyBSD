# $FreeBSD: src/gnu/usr.bin/cc/cc/Makefile,v 1.28 2002/06/05 21:30:43 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/cc/Attic/Makefile,v 1.6 2004/02/03 03:47:11 dillon Exp $

.include "../Makefile.inc"
.include "../Makefile.fe"

CFLAGS+=        -I${.CURDIR}/../config

.PATH: ${GCCDIR}/gcc ${GCCDIR}/libiberty

PROG=	cc
MAN=	gcc.1
SRCS=	gcc_local.c gccspec.c
SRCS+=	make-relative-prefix.c lrealpath.c xmalloc.c xexit.c
NOSHARED?=yes

LINKS=	${BINDIR}/cc ${BINDIR}/gcc
#MLINKS=	gcc.1 cc.1 gcc.1 c++.1 gcc.1 g++.1 gcc.1 CC.1

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"
CFLAGS+= -DCCVER=\"$(MY_CCVER)\"

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

.include <bsd.prog.mk>
