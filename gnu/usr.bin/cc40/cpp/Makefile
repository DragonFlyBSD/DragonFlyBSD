# $DragonFly: src/gnu/usr.bin/cc40/cpp/Attic/Makefile,v 1.1 2005/06/05 22:43:21 corecode Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

PROG=	cpp
MAN=	cpp40.1

SRCS=	gcc.c cppspec.c intl.c prefix.c version_local.c

SRCS+=	insn-constants.h insn-flags.h specs.h options.h

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target}\"

.include <bsd.prog.mk>
