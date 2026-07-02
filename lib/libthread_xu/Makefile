# $FreeBSD: src/lib/libpthread/Makefile,v 1.53 2004/10/24 15:32:32 ru Exp $

LIB=thread_xu
SHLIB_MAJOR= 2

# for now do not let the use of -flto
.if ${CFLAGS:M-flto}
CFLAGS+=	-fno-lto
LDFLAGS+=	-flto
.endif

CFLAGS+=-DPTHREAD_KERNEL -D_THREAD_SAFE
CFLAGS+=-I${.CURDIR}/../libc/include -I${.CURDIR}/thread \
	-I${.CURDIR}/../../include
CFLAGS+=-I${.CURDIR}/arch/${MACHINE_ARCH}/include
CFLAGS+=-I${.CURDIR}/../libthread_db
CFLAGS+=-I${.CURDIR}/../../libexec/rtld-elf

# XXX this breaks threaded applications
#LDFLAGS=-Wl,-version-script=${.CURDIR}/pthread.map

# enable extra internal consistancy checks
CFLAGS+=-D_PTHREADS_INVARIANTS

# debugging
.if defined(PTHREADS_DEBUGGING)
CFLAGS+=-D_PTHREADS_DEBUGGING
.endif
.if defined(PTHREADS_DEBUGGING2)
CFLAGS+=-D_PTHREADS_DEBUGGING2
.endif


# libthread_xu cannot be safely unloaded.  Loading it has irreversible
# process-wide effects: libc lazily binds its weak pthread/_spinlock
# stubs to the strong definitions in this library, __isthreaded stays
# set, and _thr_signal_init() installs signal handlers pointing into
# this library.  After a dlclose() unmaps it, the next locked malloc
# operation (or signal delivery) jumps through a dangling pointer and
# crashes.  Mark the library DF_1_NODELETE so dlclose() never unmaps
# it, mirroring FreeBSD libthr.
LDFLAGS+=	-Wl,-znodelete

PRECIOUSLIB=	yes

LIBDIR=	/usr/lib/thread
NOPROFILE=	yes

WARNS?=	5 # keep for -Os, lots of -Winline

.include "${.CURDIR}/arch/${MACHINE_ARCH}/Makefile.inc"
.include "${.CURDIR}/thread/Makefile.inc"

.include <bsd.lib.mk>
