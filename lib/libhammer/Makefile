# DragonflyBSD Makefile

LIB=	hammer
SRCS=	crc32.c info.c misc.c
INCS=	libhammer.h

SRCS+= crc32.c

MAN+=	 libhammer_get_volinfo.3

MLINKS+= libhammer_get_volinfo.3 libhammer_get_next_pfs.3
MLINKS+= libhammer_get_volinfo.3 libhammer_get_prev_pfs.3
MLINKS+= libhammer_get_volinfo.3 libhammer_get_first_pfs.3
MLINKS+= libhammer_get_volinfo.3 libhammer_get_last_pfs.3
MLINKS+= libhammer_get_volinfo.3 libhammer_free_volinfo.3

.PATH: ${.CURDIR}/../../sys/libkern
SHLIB_MAJOR=	0

.include <bsd.lib.mk>
