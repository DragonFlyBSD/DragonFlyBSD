# $DragonFly: src/gnu/usr.bin/cc40/gcov/Attic/Makefile,v 1.1 2005/06/05 22:43:21 corecode Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

PROG=	gcov
MAN=	gcov40.1

SRCS=	gcov.c intl.c version_local.c errors.c

SRCS+=	insn-constants.h gcov-iov.h insn-flags.h

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target}\"

.include <bsd.prog.mk>
