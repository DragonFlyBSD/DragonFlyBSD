# $DragonFly: src/lib/libkinfo/Makefile,v 1.2 2004/11/18 14:33:01 joerg Exp $

LIB=	kinfo
SRCS=	kinfo_file.c kinfo_vfs.c
INCS=	kinfo.h
NOMAN=

CFLAGS+=	-I${.CURDIR}
WARNS?=	6

SHLIB_MAJOR?= 1

.include <bsd.lib.mk>
