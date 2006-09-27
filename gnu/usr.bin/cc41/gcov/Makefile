# $DragonFly: src/gnu/usr.bin/cc41/gcov/Makefile,v 1.1 2006/09/27 12:10:35 corecode Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

PROG=	gcov
MAN=	gcov41.1

SRCS=	gcov.c intl.c errors.c version.c

SRCS+=	insn-constants.h gcov-iov.h insn-flags.h

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target}\"

gcov41.1: ${GCCDIR}/gcc/doc/gcov.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	gcov41.1

.include <bsd.prog.mk>
