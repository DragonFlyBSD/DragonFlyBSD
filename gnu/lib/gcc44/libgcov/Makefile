.include "../Makefile.inc"

CFLAGS+=	-I.
CFLAGS+=	-DIN_LIBGCC2 -D__GCC_FLOAT_NOT_NEEDED -DHAVE_GTHR_DEFAULT
CFLAGS+=	-DIN_GCC

LIB=		gcov

# used by shared libs
INSTALL_PIC_ARCHIVE=	yes

# from gcc/Makefile
LIBGCOV = _gcov _gcov_merge_add _gcov_merge_single _gcov_merge_delta \
    _gcov_fork _gcov_execl _gcov_execlp _gcov_execle \
    _gcov_execv _gcov_execvp _gcov_execve \
    _gcov_interval_profiler _gcov_pow2_profiler _gcov_one_value_profiler \
    _gcov_indirect_call_profiler _gcov_average_profiler _gcov_ior_profiler \
    _gcov_merge_ior

SRCS=	${LIBGCOV:S/$/.c/g}
CLEANFILES+= ${LIBGCOV:S/$/.c/g}

.for F in ${LIBGCOV}
${F}.c:
	echo '#define L${.TARGET:T:R}' > ${.TARGET}
	echo '#include "libgcov.c"' >> ${.TARGET}
.endfor

.include <bsd.lib.mk>
