#
# $DragonFly: src/sbin/hammer/Makefile,v 1.2 2008/01/03 06:48:45 dillon Exp $

PROG=	hammer
SRCS=	hammer.c buffer_alist.c ondisk.c cache.c super_alist.c
MAN=	hammer.8

CFLAGS+= -I${.CURDIR}/../../sys -DALIST_NO_DEBUG

.PATH: ${.CURDIR}/../../sys/libkern
SRCS+= crc32.c
.PATH: ${.CURDIR}/../../sys/vfs/hammer
SRCS+= hammer_alist.c

.include <bsd.prog.mk>
