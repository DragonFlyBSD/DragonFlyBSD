# $DragonFly: src/lib/libkcore/Makefile,v 1.2 2004/12/22 11:01:49 joerg Exp $

.PATH: ${.CURDIR}/../../sys/kern

LIB=	kcore
SRCS=	kcore.c kcore_file.c kcore_proc.c kcore_sched.c kcore_tty.c
SRCS+=	kcore_vfs.c
SRCS+=	subr_kcore.c
INCS=	kcore.h
NOMAN=

CFLAGS+=	-I${.CURDIR} -g
WARNS?=	6

SHLIB_MAJOR?= 1

.include <bsd.lib.mk>
