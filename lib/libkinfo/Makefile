# $DragonFly: src/lib/libkinfo/Makefile,v 1.1 2004/11/18 13:57:35 joerg Exp $

LIB=	kinfo
SRCS=	kinfo_file.c
INCS=	kinfo.h
NOMAN=

CFLAGS+=	-I${.CURDIR} -g -gdwarf-2
WARNS?=	6

SHLIB_MAJOR?= 1

.include <bsd.lib.mk>
