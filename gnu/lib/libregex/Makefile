# $FreeBSD: src/gnu/lib/libregex/Makefile,v 1.22.2.1 2002/07/22 14:21:45 ru Exp $
# $DragonFly: src/gnu/lib/libregex/Makefile,v 1.4 2005/05/03 07:19:03 joerg Exp $

SUBDIR+= doc

LIB=		gnuregex
SHLIB_MAJOR=	3

SRCS=	regex.c
INCS=	regex.h
INCSNAME= gnuregex.h
NOMAN=	noman

CFLAGS+=-I${.CURDIR} -DHAVE_STRING_H=1 -DSTDC_HEADERS=1 -DNO_POSIX_COMPAT=1

.include <bsd.lib.mk>
