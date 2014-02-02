# $FreeBSD: src/lib/libc_r/Makefile,v 1.24.2.7 2002/10/22 14:44:02 fjoe Exp $
#
LIB=c_r
SHLIB_MAJOR= 5
WARNS?=2
CFLAGS+=-I${.CURDIR}/../libc/include
CFLAGS+=-DPTHREAD_KERNEL -D_THREAD_SAFE -I${.CURDIR}/uthread
CFLAGS+=-I${.CURDIR}/../../include
CFLAGS+=-I${.CURDIR}/../libc/${MACHINE_ARCH}

# Uncomment this if you want libc_r to contain debug information for
# thread locking.
CFLAGS+=-D_LOCK_DEBUG

# enable extra internal consistancy checks
CFLAGS+=-D_PTHREADS_INVARIANTS

PRECIOUSLIB=	yes

LIBDIR=	/usr/lib/thread
NOPROFILE=	yes

.include "${.CURDIR}/uthread/Makefile.inc"
.include "${.CURDIR}/sys/Makefile.inc"

.include <bsd.lib.mk>
