# $DragonFly: src/gnu/lib/gcc34/libgcov/Makefile,v 1.1 2004/06/14 22:27:52 joerg Exp $

.include "../Makefile.inc"
.include "${STOPDIR}/cc_tools/Makefile.tools"

CFLAGS+=	-I.
CFLAGS+=	-DIN_LIBGCC2 -D__GCC_FLOAT_NOT_NEEDED -DHAVE_GTHR_DEFAULT
CFLAGS+=	-DIN_GCC

LIB=		gcov

# used by shared libs
INSTALL_PIC_ARCHIVE=	yes

FUNCS=	_gcov _gcov_merge_add _gcov_merge_delta _gcov_merge_single

SRCS=	${FUNCS:S/$/.c/g}
SRCS+=	insn-flags.h insn-constants.h

.for F in ${FUNCS}
${F}.c:
	echo '#define L${.TARGET:T:R}' > ${.TARGET}
	echo '#include "libgcc2.c"' >> ${.TARGET}
.endfor

.include <bsd.lib.mk>
