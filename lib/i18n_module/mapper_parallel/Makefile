# $FreeBSD: head/lib/libiconv_modules/mapper_parallel/Makefile 219019 2011-02-25 00:04:39Z gabor $

.PATH: ${.CURDIR}/../mapper_serial

LIB=	mapper_parallel
SRCS+=	citrus_mapper_serial.c
CFLAGS+= --param max-inline-insns-single=32

.include <bsd.lib.mk>
