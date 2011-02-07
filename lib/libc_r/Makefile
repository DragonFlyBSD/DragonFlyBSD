# $FreeBSD: src/lib/libc_r/Makefile,v 1.24.2.7 2002/10/22 14:44:02 fjoe Exp $
#
# All library objects contain rcsid strings by default; they may be
# excluded as a space-saving measure.  To produce a library that does
# not contain these strings, delete -DLIBC_RCS and -DSYSLIBC_RCS
# from CFLAGS below.  To remove these strings from just the system call
# stubs, remove just -DSYSLIBC_RCS from CFLAGS.
LIB=c_r
SHLIB_MAJOR= 5
CSTD?=gnu89
WARNS?=1
CFLAGS+=-DLIBC_RCS -DSYSLIBC_RCS -I${.CURDIR}/../libc/include
CFLAGS+=-DPTHREAD_KERNEL -D_THREAD_SAFE -I${.CURDIR}/uthread
CFLAGS+=-I${.CURDIR}/../../include

# Uncomment this if you want libc_r to contain debug information for
# thread locking.
CFLAGS+=-D_LOCK_DEBUG

# enable extra internal consistancy checks
CFLAGS+=-D_PTHREADS_INVARIANTS

AINC=	-I${.CURDIR}/../libc/${MACHINE_ARCH} -I${.CURDIR}/uthread
PRECIOUSLIB=	yes

LIBDIR=	/usr/lib/thread
NOPROFILE=	yes

.include "${.CURDIR}/uthread/Makefile.inc"
.include "${.CURDIR}/sys/Makefile.inc"

.include <bsd.lib.mk>
