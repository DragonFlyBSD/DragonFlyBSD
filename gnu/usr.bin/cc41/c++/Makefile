# $DragonFly: src/gnu/usr.bin/cc41/c++/Makefile,v 1.1 2006/09/27 12:10:33 corecode Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

.PATH:	${GCCDIR}/gcc/cp

PROG=	c++
LINKS=	${BINDIR}/c++ ${BINDIR}/g++
LINKS+=	${BINDIR}/c++ ${BINDIR}/CC
MAN=	g++41.1

SRCS=	gcc.c g++spec.c intl.c prefix.c version.c

SRCS+=	insn-constants.h insn-flags.h specs.h options.h

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target_machine}\"

g++41.1: ${GCCDIR}/gcc/doc/g++.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	g++41.1

.include <bsd.prog.mk>
