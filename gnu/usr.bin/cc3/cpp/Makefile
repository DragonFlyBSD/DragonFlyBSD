# $FreeBSD: src/gnu/usr.bin/cc/cpp/Makefile,v 1.23 2003/06/06 13:46:55 kuriyama Exp $
# $DragonFly: src/gnu/usr.bin/cc3/cpp/Attic/Makefile,v 1.10 2004/06/21 03:48:05 dillon Exp $

.include "../Makefile.inc"
.include "../Makefile.fe"

CFLAGS+=        -I${.CURDIR}/../config

.PATH: ${GCCDIR}/gcc ${GCCDIR}/libiberty

PROG=	cpp
SRCS=	gcc_local.c cppspec.c
SRCS+=  make-relative-prefix.c lrealpath.c xmalloc.c xexit.c

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

CLEANFILES+= cpp.1

cpp.1: cccp.1
	echo ${.TARGET}
	cat ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>
