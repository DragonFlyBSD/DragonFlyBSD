# $FreeBSD: src/gnu/usr.bin/cc/f77/Makefile,v 1.12.2.2 2002/11/15 18:18:39 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc/f77/Attic/Makefile,v 1.7 2004/06/21 03:48:05 dillon Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}/f ${GCCDIR}

PROG=	f77
SRCS=	gcc.c g77spec.c version.c

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"
CFLAGS+= -DBINUTILS=\"$(MY_BINUTILS)\"

DPADD=	${LIBCC_INT}
LDADD=	${LIBCC_INT}

CLEANFILES= f77.1

f77.1: g77.1
	ln -sf ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
