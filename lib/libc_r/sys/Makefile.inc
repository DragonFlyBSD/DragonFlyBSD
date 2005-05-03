# $FreeBSD: src/lib/libc_r/sys/Makefile.inc,v 1.10 1999/08/28 00:03:13 peter Exp $
# $DragonFly: src/lib/libc_r/sys/Makefile.inc,v 1.3 2005/05/03 07:29:04 joerg Exp $

.PATH:	 ${.CURDIR}/sys ${.CURDIR}/arch/${MACHINE_ARCH}

SRCS+=	_atomic_lock.S

