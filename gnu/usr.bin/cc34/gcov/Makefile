# $DragonFly: src/gnu/usr.bin/cc34/gcov/Makefile,v 1.4 2007/01/13 16:32:16 corecode Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

PROG=	gcov

SRCS=	gcov.c intl.c version_local.c
SRCS+=	insn-constants.h gcov-iov.h insn-flags.h
SRCS+=	xexit.c xmalloc.c xstrdup.c

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target}\"

gcov.1: ${GCCDIR}/gcc/doc/gcov.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	gcov.1

.include <bsd.prog.mk>
