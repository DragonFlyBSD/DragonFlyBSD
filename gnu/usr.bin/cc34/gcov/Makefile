# $DragonFly: src/gnu/usr.bin/cc34/gcov/Makefile,v 1.1 2004/06/14 22:27:56 joerg Exp $

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

.include <bsd.prog.mk>
