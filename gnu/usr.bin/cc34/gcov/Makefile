# $DragonFly: src/gnu/usr.bin/cc34/gcov/Makefile,v 1.2 2004/06/20 20:59:21 joerg Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

PROG=	gcov

SRCS=	gcov.c intl.c version_local.c
SRCS+=	insn-constants.h gcov-iov.h insn-flags.h
SRCS+=	xexit.c xmalloc.c xstrdup.c

.if defined(BOOTSTRAPPING)
SRCS+=	getopt_long.c
.endif

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target}\"

.include <bsd.prog.mk>
