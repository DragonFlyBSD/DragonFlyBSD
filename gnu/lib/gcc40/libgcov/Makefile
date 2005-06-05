# $DragonFly: src/gnu/lib/gcc40/libgcov/Attic/Makefile,v 1.1 2005/06/05 22:43:21 corecode Exp $

.include "../Makefile.inc"
.include "${STOPDIR}/cc_tools/Makefile.tools"

CFLAGS+=	-I.
CFLAGS+=	-DIN_LIBGCC2 -D__GCC_FLOAT_NOT_NEEDED -DHAVE_GTHR_DEFAULT
CFLAGS+=	-DIN_GCC

LIB=		gcov

# used by shared libs
INSTALL_PIC_ARCHIVE=	yes

FUNCS=	_gcov _gcov_merge_add _gcov_merge_single _gcov_merge_delta \
	_gcov_fork _gcov_execl _gcov_execlp _gcov_execle \
	_gcov_execv _gcov_execvp _gcov_execve


SRCS=	${FUNCS:S/$/.c/g}
CLEANFILES+= ${FUNCS:S/$/.c/g}
SRCS+=	insn-flags.h insn-constants.h

.for F in ${FUNCS}
${F}.c:
	echo '#define L${.TARGET:T:R}' > ${.TARGET}
	echo '#include "libgcc2.c"' >> ${.TARGET}
.endfor

.include <bsd.lib.mk>
