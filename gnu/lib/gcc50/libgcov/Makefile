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

# taken from build/x86_64-portbld-dragonfly4.1/libgcc/Makefile
LIBGCOV_MERGE = _gcov_merge_add _gcov_merge_single _gcov_merge_delta	\
	_gcov_merge_ior _gcov_merge_time_profile _gcov_merge_icall_topn
LIBGCOV_PROFILER = _gcov_interval_profiler _gcov_pow2_profiler		\
	_gcov_one_value_profiler _gcov_indirect_call_profiler		\
 	_gcov_average_profiler _gcov_ior_profiler			\
	_gcov_indirect_call_profiler_v2 _gcov_time_profiler		\
	_gcov_indirect_call_topn_profiler
LIBGCOV_INTERFACE = _gcov_dump _gcov_flush _gcov_fork			\
	_gcov_execl _gcov_execlp					\
	_gcov_execle _gcov_execv _gcov_execvp _gcov_execve _gcov_reset
LIBGCOV_DRIVER = _gcov

LIBGCOV=	${LIBGCOV_MERGE} ${LIBGCOV_PROFILER} ${LIBGCOV_INTERFACE} \
		${LIBGCOV_DRIVER}

OBJS=	${LIBGCOV:S/$/.o/}

gthr-default.h: gthr-posix.h
	cp ${.ALLSRC} ${.TARGET}

${LIBGCOV_MERGE:S/$/.o/}: ${GCCDIR}/libgcc/libgcov-merge.c
	${CC} ${STATIC_CFLAGS} ${CFLAGS} -DL${.TARGET:R} -c ${.ALLSRC} \
		-o ${.TARGET}

${LIBGCOV_PROFILER:S/$/.o/}: ${GCCDIR}/libgcc/libgcov-profiler.c
	${CC} ${STATIC_CFLAGS} ${CFLAGS} -DL${.TARGET:R} -c ${.ALLSRC} \
		-o ${.TARGET}

${LIBGCOV_INTERFACE:S/$/.o/}: ${GCCDIR}/libgcc/libgcov-interface.c gthr-default.h
	${CC} ${STATIC_CFLAGS} ${CFLAGS} -DL${.TARGET:R} -c ${.ALLSRC:M*.c} \
		-o ${.TARGET}

${LIBGCOV_DRIVER:S/$/.o/}: ${GCCDIR}/libgcc/libgcov-driver.c
	${CC} ${STATIC_CFLAGS} ${CFLAGS} -DL${.TARGET:R} -c ${.ALLSRC} \
		-o ${.TARGET}

# profile versions follow

${LIBGCOV_MERGE:S/$/.po/}: ${GCCDIR}/libgcc/libgcov-merge.c
	${CC} ${PO_FLAG} ${STATIC_CFLAGS} ${PO_CFLAGS} -DL${.TARGET:R} \
		-c ${.ALLSRC} -o ${.TARGET}

${LIBGCOV_PROFILER:S/$/.po/}: ${GCCDIR}/libgcc/libgcov-profiler.c
	${CC} ${PO_FLAG} ${STATIC_CFLAGS} ${PO_CFLAGS} -DL${.TARGET:R} \
		-c ${.ALLSRC} -o ${.TARGET}

${LIBGCOV_INTERFACE:S/$/.po/}: ${GCCDIR}/libgcc/libgcov-interface.c gthr-default.h
	${CC} ${PO_FLAG} ${STATIC_CFLAGS} ${PO_CFLAGS} -DL${.TARGET:R} \
		-c ${.ALLSRC:M*.c} -o ${.TARGET}

${LIBGCOV_DRIVER:S/$/.po/}: ${GCCDIR}/libgcc/libgcov-driver.c
	${CC} ${PO_FLAG} ${STATIC_CFLAGS} ${PO_CFLAGS} -DL${.TARGET:R} \
		-c ${.ALLSRC} -o ${.TARGET}

# pic versions follow

${LIBGCOV_MERGE:S/$/.So/}: ${GCCDIR}/libgcc/libgcov-merge.c
	${CC} ${PICFLAG} -DPIC ${SHARED_CFLAGS} ${CFLAGS} -DL${.TARGET:R} -c ${.ALLSRC} \
		-o ${.TARGET}

${LIBGCOV_PROFILER:S/$/.So/}: ${GCCDIR}/libgcc/libgcov-profiler.c
	${CC} ${PICFLAG} -DPIC ${SHARED_CFLAGS} ${CFLAGS} -DL${.TARGET:R} -c ${.ALLSRC} \
		-o ${.TARGET}

${LIBGCOV_INTERFACE:S/$/.So/}: ${GCCDIR}/libgcc/libgcov-interface.c gthr-default.h
	${CC} ${PICFLAG} -DPIC ${SHARED_CFLAGS} ${CFLAGS} -DL${.TARGET:R} -c ${.ALLSRC:M*.c} \
		-o ${.TARGET}

${LIBGCOV_DRIVER:S/$/.So/}: ${GCCDIR}/libgcc/libgcov-driver.c
	${CC} ${PICFLAG} -DPIC ${SHARED_CFLAGS} ${CFLAGS} -DL${.TARGET:R} -c ${.ALLSRC} \
		-o ${.TARGET}

.include <bsd.lib.mk>
