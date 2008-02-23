#
# $DragonFly: src/sbin/hammer/Makefile,v 1.6 2008/02/23 03:01:06 dillon Exp $

PROG=	hammer
SRCS=	hammer.c ondisk.c blockmap.c cache.c \
	misc.c cmd_show.c cmd_prune.c cmd_history.c
MAN=	hammer.8

CFLAGS+= -I${.CURDIR}/../../sys -DALIST_NO_DEBUG

.PATH: ${.CURDIR}/../../sys/libkern
SRCS+= crc32.c

.include <bsd.prog.mk>
