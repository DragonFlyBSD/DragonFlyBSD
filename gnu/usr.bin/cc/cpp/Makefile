# $FreeBSD: src/gnu/usr.bin/cc/cpp/Makefile,v 1.18.2.2 2002/11/15 18:18:39 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc/cpp/Attic/Makefile,v 1.3 2004/01/16 07:45:22 dillon Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}

PROG=	cpp
SRCS=	gcc.c cppspec.c
NOMAN=	1

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"
CFLAGS+= -DGCCVER=\"$(GCCVER)\"

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

.include <bsd.prog.mk>
