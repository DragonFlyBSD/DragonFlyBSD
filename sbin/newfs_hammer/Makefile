#
# $DragonFly: src/sbin/newfs_hammer/Makefile,v 1.4 2008/02/08 08:30:58 dillon Exp $

PROG=	newfs_hammer
MAN=	newfs_hammer.8
CFLAGS+= -I${.CURDIR}/../../sys -I${.CURDIR}/../hammer -DALIST_NO_DEBUG
SRCS= newfs_hammer.c

.PATH: ${.CURDIR}/../../sys/libkern
SRCS+= crc32.c
.PATH: ${.CURDIR}/../hammer
SRCS+= ondisk.c cache.c

.include <bsd.prog.mk>
