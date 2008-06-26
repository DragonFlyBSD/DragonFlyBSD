#
# $DragonFly: src/sbin/hammer/Makefile,v 1.17 2008/06/26 04:07:57 dillon Exp $

PROG=	hammer
SRCS=	hammer.c ondisk.c blockmap.c cache.c misc.c cycle.c \
	cmd_show.c cmd_softprune.c cmd_history.c \
	cmd_blockmap.c cmd_reblock.c cmd_synctid.c cmd_stats.c \
	cmd_pseudofs.c cmd_snapshot.c cmd_mirror.c cmd_status.c
MAN=	hammer.8

CFLAGS+= -I${.CURDIR}/../../sys -DALIST_NO_DEBUG
LDADD= -lm

.PATH: ${.CURDIR}/../../sys/libkern
SRCS+= crc32.c

.include <bsd.prog.mk>
