PROG=	hammer
SRCS=	hammer.c ondisk.c blockmap.c cache.c misc.c cycle.c \
	cmd_show.c cmd_softprune.c cmd_history.c \
	cmd_blockmap.c cmd_reblock.c cmd_rebalance.c \
	cmd_synctid.c cmd_stats.c cmd_remote.c \
	cmd_pseudofs.c cmd_snapshot.c cmd_mirror.c \
	cmd_cleanup.c cmd_info.c cmd_version.c cmd_volume.c \
	cmd_config.c cmd_recover.c cmd_dedup.c cmd_abort.c
MAN=	hammer.8

CFLAGS+= -I${.CURDIR}/../../sys -DALIST_NO_DEBUG
LDADD=	-lm -lutil -lmd -lhammer
DPADD=	${LIBM} ${LIBUTIL} ${LIBMD} ${LIBHAMMER}

.PATH: ${.CURDIR}/../../sys/libkern
SRCS+= crc32.c

.include <bsd.prog.mk>
