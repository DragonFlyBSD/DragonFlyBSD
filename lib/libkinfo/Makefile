# $DragonFly: src/lib/libkinfo/Makefile,v 1.3 2004/12/22 11:01:49 joerg Exp $

LIB=	kinfo
SRCS=	kinfo_file.c kinfo_sched.c kinfo_tty.c kinfo_vfs.c
INCS=	kinfo.h
NOMAN=

CFLAGS+=	-I${.CURDIR}
WARNS?=	6

SHLIB_MAJOR?= 1

.include <bsd.lib.mk>
