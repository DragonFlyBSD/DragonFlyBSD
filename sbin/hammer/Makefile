#
# $DragonFly: src/sbin/hammer/Makefile,v 1.10 2008/05/11 20:44:44 dillon Exp $

PROG=	hammer
SRCS=	hammer.c ondisk.c blockmap.c cache.c misc.c cycle.c \
	cmd_show.c cmd_prune.c cmd_history.c cmd_blockmap.c \
	cmd_reblock.c
MAN=	hammer.8

CFLAGS+= -I${.CURDIR}/../../sys -DALIST_NO_DEBUG
LDADD= -lm

.PATH: ${.CURDIR}/../../sys/libkern
SRCS+= crc32.c

.include <bsd.prog.mk>
