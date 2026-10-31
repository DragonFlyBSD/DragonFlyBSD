.include "../Makefile.inc"
.PATH: ${GCCDIR}/libgcc

LIB=		gcov
WARNS?=1

CFLAGS+=	-I.
CFLAGS+=	-I${GCCDIR}/libgcc
CFLAGS+=	-I${.OBJDIR}/../csu
CFLAGS+=	-fbuilding-libgcc
CFLAGS+=	-fno-stack-protector
CFLAGS+=	-DIN_LIBGCC2 -DHAVE_CC_TLS -DIN_GCC

# used by shared libs
INSTALL_PIC_ARCHIVE=	yes

# taken from build/x86_64-portbld-dragonfly5.3/libgcc/Makefile
LIBGCOV_MERGE = _gcov_merge_add _gcov_merge_single			\
	_gcov_merge_ior _gcov_merge_time_profile _gcov_merge_icall_topn
LIBGCOV_PROFILER = _gcov_interval_profiler				\
	_gcov_interval_profiler_atomic					\
	_gcov_pow2_profiler						\
	_gcov_pow2_profiler_atomic					\
	_gcov_one_value_profiler					\
	_gcov_one_value_profiler_atomic					\
	_gcov_average_profiler						\
	_gcov_average_profiler_atomic					\
	_gcov_ior_profiler						\
	_gcov_ior_profiler_atomic					\
	_gcov_indirect_call_profiler_v2					\
	_gcov_time_profiler						\
	_gcov_indirect_call_topn_profiler
LIBGCOV_INTERFACE = _gcov_dump _gcov_flush _gcov_fork			\
	_gcov_execl _gcov_execlp					\
	_gcov_execle _gcov_execv _gcov_execvp _gcov_execve _gcov_reset
LIBGCOV_DRIVER = _gcov

LIBGCOV=	${LIBGCOV_MERGE} ${LIBGCOV_PROFILER} ${LIBGCOV_INTERFACE} \
		${LIBGCOV_DRIVER}

OBJS=	${LIBGCOV:S/$/.o/}

LIBGCOV_SRC_MERGE=	${GCCDIR}/libgcc/libgcov-merge.c
LIBGCOV_SRC_PROFILER=	${GCCDIR}/libgcc/libgcov-profiler.c
LIBGCOV_SRC_INTERFACE=	${GCCDIR}/libgcc/libgcov-interface.c gthr-default.h
LIBGCOV_SRC_DRIVER=	${GCCDIR}/libgcc/libgcov-driver.c

.for _g in MERGE PROFILER INTERFACE DRIVER
. for f in ${LIBGCOV_${_g}}
SRC.${f}.o=	${LIBGCOV_SRC_${_g}}
FLAGS.${f}.o=	-DL${f}
. endfor
.endfor

gthr-default.h: gthr-posix.h
	cp ${.ALLSRC} ${.TARGET}

auto-target.h: ${.CURDIR}/../libgcc/auto-target.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+= auto-target.h gthr-default.h

beforedepend: auto-target.h

.include <bsd.lib.mk>
