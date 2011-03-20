.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

PROG=	cpp
MAN=	cpp41.1

SRCS=	gcc.c cppspec.c intl.c prefix.c version.c

SRCS+=	insn-constants.h insn-flags.h specs.h options.h

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target_machine}\"

cpp41.1: ${GCCDIR}/gcc/doc/cpp.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	cpp41.1

.include <bsd.prog.mk>
