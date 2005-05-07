# $FreeBSD: src/lib/libpthread/Makefile,v 1.53 2004/10/24 15:32:32 ru Exp $
# $DragonFly: src/lib/libthread_xu/Makefile,v 1.5 2005/05/07 09:29:46 davidxu Exp $
#
# All library objects contain FreeBSD revision strings by default; they may be
# excluded as a space-saving measure.  To produce a library that does
# not contain these strings, add -DSTRIP_FBSDID (see <sys/cdefs.h>) to CFLAGS
# below.  Note, there are no IDs for syscall stubs whose sources are generated.
# To included legacy CSRG sccsid strings, add -DLIBC_SCCS and -DSYSLIBC_SCCS
# (for system call stubs) to CFLAGS below.  -DSYSLIBC_SCCS affects just the
# system call stubs.
LIB=thread_xu
SHLIB_MAJOR= 2
CFLAGS+=-DPTHREAD_KERNEL -D_THREAD_SAFE
CFLAGS+=-I${.CURDIR}/../libc/include -I${.CURDIR}/thread \
	-I${.CURDIR}/../../include
CFLAGS+=-I${.CURDIR}/arch/${MACHINE_ARCH}/include
CFLAGS+=-I${.CURDIR}/../libthread_db
CFLAGS+=-Winline

CFLAGS+= -g

# CFLAGS+=-DSYSTEM_SCOPE_ONLY

LDFLAGS=--version-script=${.CURDIR}/pthread.map

# enable extra internal consistancy checks
CFLAGS+=-D_PTHREADS_INVARIANTS -Wall

PRECIOUSLIB=	yes

.include "${.CURDIR}/arch/${MACHINE_ARCH}/Makefile.inc"
.include "${.CURDIR}/thread/Makefile.inc"

.include <bsd.lib.mk>
