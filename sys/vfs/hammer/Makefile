#
# $DragonFly: src/sys/vfs/hammer/Makefile,v 1.13.2.1 2008/07/17 10:18:06 swildner Exp $

KMOD=	hammer
SRCS=	hammer_vfsops.c hammer_vnops.c hammer_inode.c \
	hammer_subs.c hammer_ondisk.c hammer_io.c hammer_signal.c \
	hammer_cursor.c hammer_btree.c hammer_transaction.c \
	hammer_object.c hammer_recover.c hammer_ioctl.c \
	hammer_blockmap.c hammer_freemap.c hammer_undo.c \
	hammer_reblock.c hammer_flusher.c hammer_mirror.c \
	hammer_pfs.c hammer_prune.c
SRCS+=	opt_ktr.h

.include <bsd.kmod.mk>
