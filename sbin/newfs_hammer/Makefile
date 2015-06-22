#
# $DragonFly: src/sbin/newfs_hammer/Makefile,v 1.5 2008/02/23 03:01:07 dillon Exp $

PROG=	newfs_hammer
MAN=	newfs_hammer.8
CFLAGS+= -I${.CURDIR}/../../sys -I${.CURDIR}/../hammer -DALIST_NO_DEBUG
SRCS= newfs_hammer.c

.PATH: ${.CURDIR}/../../sys/libkern
SRCS+= crc32.c icrc32.c
.PATH: ${.CURDIR}/../hammer
SRCS+= ondisk.c cache.c blockmap.c

.include <bsd.prog.mk>
