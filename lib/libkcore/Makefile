# $DragonFly: src/lib/libkcore/Makefile,v 1.4 2005/05/01 02:30:50 hmp Exp $

.PATH: ${.CURDIR}/../../sys/kern ${.CURDIR}/../../lib/libkinfo

LIB=	kcore
SRCS=	kcore.c kcore_file.c kcore_proc.c kcore_sched.c kcore_tty.c
SRCS+=	kinfo_pcpu.c
SRCS+=	kcore_vfs.c
SRCS+=	subr_kcore.c
INCS=	kcore.h
NOMAN=

CFLAGS+=	-I${.CURDIR} -g
WARNS?=	6

SHLIB_MAJOR?= 2

.include <bsd.lib.mk>
