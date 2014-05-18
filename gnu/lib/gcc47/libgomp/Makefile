CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
.include "../Makefile.inc"
.include "Makefile.${TARGET_ARCH}"

.PATH: ${GCCDIR}/libgomp
.PATH: ${GCCDIR}/libgomp/config/bsd
.PATH: ${GCCDIR}/libgomp/config/posix

CFLAGS+=	-DHAVE_CONFIG_H
CFLAGS+=	-I${GCCDIR}/libgomp
CFLAGS+=	-I${GCCDIR}/libgomp/config/posix
CFLAGS+=	-I${GCCDIR}/libgcc
LDFLAGS+=	-Wl,--version-script=${GCCDIR}/libgomp/libgomp.map

LIB=		gomp
SHLIB_MAJOR=	1

LDADD=		-pthread
DPADD=		${LIBPTHREAD}

# From libgomp Makefile
libgomp_la_SOURCES = alloc.c barrier.c critical.c env.c error.c iter.c \
	iter_ull.c loop.c loop_ull.c ordered.c parallel.c sections.c single.c \
	task.c team.c work.c lock.c mutex.c proc.c sem.c bar.c ptrlock.c \
	time.c fortran.c affinity.c

SRCS=	${libgomp_la_SOURCES}

# generated
SRCS+=	libgomp_f.h

libgomp_f.h: ${GCCDIR}/libgomp/libgomp_f.h.in
	sed -e 's/@OMP_LOCK_25_ALIGN@/${OMP_LOCK_25_ALIGN}/g' \
	    -e 's/@OMP_LOCK_25_KIND@/${OMP_LOCK_25_KIND}/g' \
	    -e 's/@OMP_LOCK_25_SIZE@/${OMP_LOCK_25_SIZE}/g' \
	    -e 's/@OMP_LOCK_ALIGN@/${OMP_LOCK_ALIGN}/g' \
	    -e 's/@OMP_LOCK_KIND@/${OMP_LOCK_KIND}/g' \
	    -e 's/@OMP_LOCK_SIZE@/${OMP_LOCK_SIZE}/g' \
	    -e 's/@OMP_NEST_LOCK_25_ALIGN@/${OMP_NEST_LOCK_25_ALIGN}/g' \
	    -e 's/@OMP_NEST_LOCK_25_KIND@/${OMP_NEST_LOCK_25_KIND}/g' \
	    -e 's/@OMP_NEST_LOCK_25_SIZE@/${OMP_NEST_LOCK_25_SIZE}/g' \
	    -e 's/@OMP_NEST_LOCK_ALIGN@/${OMP_NEST_LOCK_ALIGN}/g' \
	    -e 's/@OMP_NEST_LOCK_KIND@/${OMP_NEST_LOCK_KIND}/g' \
	    -e 's/@OMP_NEST_LOCK_SIZE@/${OMP_NEST_LOCK_SIZE}/g' \
	    < ${.ALLSRC} > ${.TARGET}

omp.h: ${GCCDIR}/libgomp/omp.h.in
	sed -e 's/@OMP_LOCK_SIZE@/${OMP_LOCK_SIZE}/g' \
	    -e 's/@OMP_LOCK_ALIGN@/${OMP_LOCK_ALIGN}/g' \
	    -e 's/@OMP_NEST_LOCK_SIZE@/${OMP_NEST_LOCK_SIZE}/g' \
	    -e 's/@OMP_NEST_LOCK_ALIGN@/${OMP_NEST_LOCK_ALIGN}/g' \
	    < ${.ALLSRC} > ${.TARGET}

INCSGROUPS=	hd_libdata hd_lib
hd_libdataDIR=	/usr/libdata/gcc${GCCSHORTVER}
hd_libDIR=	/usr/lib/gcc${GCCSHORTVER}

hd_libdata=	omp.h
hd_lib= 	libgomp.spec

CLEANFILES+=	libgomp_f.h omp.h

.include <bsd.lib.mk>
