# $DragonFly: src/gnu/usr.bin/cc41/cc/Makefile,v 1.1 2006/09/27 12:10:33 corecode Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

PROG=	cc
LINKS=	${BINDIR}/cc ${BINDIR}/gcc
MAN=	gcc41.1

SRCS=	gcc.c gccspec.c intl.c prefix.c version.c

SRCS+=	insn-constants.h insn-flags.h specs.h options.h

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target}\"

gcc41.1: ${GCCDIR}/gcc/doc/gcc.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	gcc41.1

.include <bsd.prog.mk>
