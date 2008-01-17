#
# $DragonFly: src/sbin/hammer/Makefile,v 1.3 2008/01/17 04:59:48 dillon Exp $

PROG=	hammer
SRCS=	hammer.c buffer_alist.c ondisk.c cache.c super_alist.c \
	misc.c cmd_show.c
MAN=	hammer.8

CFLAGS+= -I${.CURDIR}/../../sys -DALIST_NO_DEBUG

.PATH: ${.CURDIR}/../../sys/libkern
SRCS+= crc32.c
.PATH: ${.CURDIR}/../../sys/vfs/hammer
SRCS+= hammer_alist.c

.include <bsd.prog.mk>
