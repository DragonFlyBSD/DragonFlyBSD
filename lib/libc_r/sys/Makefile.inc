# $FreeBSD: src/lib/libc_r/sys/Makefile.inc,v 1.10 1999/08/28 00:03:13 peter Exp $
# $DragonFly: src/lib/libc_r/sys/Makefile.inc,v 1.2 2003/06/17 04:26:48 dillon Exp $

.PATH:	 ${.CURDIR}/sys ${.CURDIR}/arch/${MACHINE_ARCH}

SRCS+=	uthread_error.c _atomic_lock.S

