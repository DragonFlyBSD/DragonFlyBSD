# $DragonFly: src/lib/libkinfo/Makefile,v 1.6 2005/05/01 04:05:35 hmp Exp $

LIB=	kinfo
SRCS=	kinfo_file.c kinfo_net.c kinfo_sched.c kinfo_tty.c kinfo_vfs.c
SRCS+=	kinfo_pcpu.c
INCS=	kinfo.h kinfo_pcpu.h
NOMAN=

CFLAGS+=	-I${.CURDIR}
WARNS?=	6

SHLIB_MAJOR?= 2

.include <bsd.lib.mk>
