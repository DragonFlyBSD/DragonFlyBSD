#
# $DragonFly: src/sbin/newfs_hammer/Makefile,v 1.1 2007/10/10 19:35:53 dillon Exp $

PROG=	newfs_hammer
MAN=	newfs_hammer.8
CFLAGS+= -I${.CURDIR}/../../sys -DALIST_NO_DEBUG
SRCS= crc32.c hammer_alist.c newfs_hammer.c
WARNS?=	2

.PATH: ${.CURDIR}/../../sys/libkern ${.CURDIR}/../../sys/vfs/hammer

.include <bsd.prog.mk>
