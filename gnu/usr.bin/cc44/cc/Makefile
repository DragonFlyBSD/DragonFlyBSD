.include "../Makefile.inc"
.include "../Makefile.langs"

PROG=	cc
LINKS=	${BINDIR}/cc ${BINDIR}/gcc
MAN=	gcc${GCCSHORTVER}.1

.if ${GCCDOCSUFFIX} != ${GCCSHORTVER}
MLINKS=	gcc${GCCSHORTVER}.1 cc${GCCDOCSUFFIX}.1
MLINKS+=gcc${GCCSHORTVER}.1 c++${GCCDOCSUFFIX}.1
MLINKS+=gcc${GCCSHORTVER}.1 g++${GCCDOCSUFFIX}.1
MLINKS+=gcc${GCCSHORTVER}.1 gcc${GCCDOCSUFFIX}.1
MLINKS+=gcc${GCCSHORTVER}.1 CC${GCCDOCSUFFIX}.1
.endif

SRCS=	${GCC_SRCS} gccspec.c intl.c prefix.c version.c
SRCS+=	${EXTRA_GCC_SRCS}

CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target_machine}\"
CFLAGS+= -DGCC_DRIVER

gcc${GCCSHORTVER}.1: ${GCCDIR}/gcc/doc/gcc.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	gcc${GCCSHORTVER}.1

.include <bsd.prog.mk>
