#
# $DragonFly: src/sbin/newfs_hammer/Makefile,v 1.3 2008/01/03 06:48:47 dillon Exp $

PROG=	newfs_hammer
MAN=	newfs_hammer.8
CFLAGS+= -I${.CURDIR}/../../sys -I${.CURDIR}/../hammer -DALIST_NO_DEBUG
SRCS= newfs_hammer.c

.PATH: ${.CURDIR}/../../sys/libkern
SRCS+= crc32.c
.PATH: ${.CURDIR}/../../sys/vfs/hammer
SRCS+= hammer_alist.c
.PATH: ${.CURDIR}/../hammer
SRCS+= ondisk.c cache.c super_alist.c buffer_alist.c

.include <bsd.prog.mk>
