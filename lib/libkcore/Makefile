# $DragonFly: src/lib/libkcore/Makefile,v 1.3 2005/04/21 13:42:34 joerg Exp $

.PATH: ${.CURDIR}/../../sys/kern

LIB=	kcore
SRCS=	kcore.c kcore_file.c kcore_proc.c kcore_sched.c kcore_tty.c
SRCS+=	kcore_vfs.c
SRCS+=	subr_kcore.c
INCS=	kcore.h
NOMAN=

CFLAGS+=	-I${.CURDIR} -g
WARNS?=	6

SHLIB_MAJOR?= 2

.include <bsd.lib.mk>
