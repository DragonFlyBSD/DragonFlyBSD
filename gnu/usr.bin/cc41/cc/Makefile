# $DragonFly: src/gnu/usr.bin/cc41/cc/Makefile,v 1.2 2007/09/08 09:48:59 swildner Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

PROG=	cc
LINKS=	${BINDIR}/cc ${BINDIR}/gcc
MAN=	gcc41.1
MLINKS=	gcc41.1 cc.1 gcc41.1 c++.1 gcc41.1 g++.1 gcc41.1 gcc.1 gcc41.1 CC.1

SRCS=	gcc.c gccspec.c intl.c prefix.c version.c

SRCS+=	insn-constants.h insn-flags.h specs.h options.h

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target}\"

gcc41.1: ${GCCDIR}/gcc/doc/gcc.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	gcc41.1

.include <bsd.prog.mk>
