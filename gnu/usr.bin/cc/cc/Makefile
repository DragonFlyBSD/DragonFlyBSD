# $FreeBSD: src/gnu/usr.bin/cc/cc/Makefile,v 1.20.2.4 2002/11/15 18:18:38 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc/cc/Attic/Makefile,v 1.8 2004/06/21 03:48:05 dillon Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}

PROG=	cc
MAN=	gcc.1
SRCS=	gcc.c gccspec.c
NOSHARED=yes

LINKS=	${BINDIR}/cc ${BINDIR}/gcc
MLINKS=	gcc.1 cc.1 gcc.1 c++.1 gcc.1 g++.1 gcc.1 CC.1

# Remove dangerous optimizer settings for the system compiler
CFLAGS:= ${CFLAGS:N-O*:N-march=*}

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"
CFLAGS+= -DBINUTILS=\"$(MY_BINUTILS)\"

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

.include <bsd.prog.mk>
