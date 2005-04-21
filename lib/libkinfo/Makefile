# $DragonFly: src/lib/libkinfo/Makefile,v 1.4 2005/04/21 13:42:34 joerg Exp $

LIB=	kinfo
SRCS=	kinfo_file.c kinfo_sched.c kinfo_tty.c kinfo_vfs.c
INCS=	kinfo.h
NOMAN=

CFLAGS+=	-I${.CURDIR}
WARNS?=	6

SHLIB_MAJOR?= 2

.include <bsd.lib.mk>
