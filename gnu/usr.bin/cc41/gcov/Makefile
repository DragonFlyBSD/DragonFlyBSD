.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

PROG=	gcov
MAN=	gcov41.1

SRCS=	gcov.c intl.c errors.c version.c

SRCS+=	insn-constants.h gcov-iov.h insn-flags.h

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target_machine}\"

gcov41.1: ${GCCDIR}/gcc/doc/gcov.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	gcov41.1

.include <bsd.prog.mk>
