# $FreeBSD: src/gnu/usr.bin/cc/f77/Makefile,v 1.12.2.2 2002/11/15 18:18:39 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc/f77/Attic/Makefile,v 1.5 2004/01/24 20:03:27 joerg Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}/f ${GCCDIR}

PROG=	f77
SRCS=	gcc.c g77spec.c version.c

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"$(version)\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"$(target)\"
CFLAGS+= -DCCVER=\"$(MY_CCVER)\"

DPADD=	${LIBCC_INT}
LDADD=	${LIBCC_INT}

CLEANFILES= f77.1

f77.1: g77.1
	ln -sf ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
