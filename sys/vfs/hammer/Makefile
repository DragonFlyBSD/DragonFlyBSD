#
# $DragonFly: src/sys/vfs/hammer/Makefile,v 1.12 2008/06/26 04:06:22 dillon Exp $

KMOD=	hammer
SRCS=	hammer_vfsops.c hammer_vnops.c hammer_inode.c \
	hammer_subs.c hammer_ondisk.c hammer_io.c hammer_signal.c \
	hammer_cursor.c hammer_btree.c hammer_transaction.c \
	hammer_object.c hammer_recover.c hammer_ioctl.c \
	hammer_blockmap.c hammer_freemap.c hammer_undo.c \
	hammer_reblock.c hammer_flusher.c hammer_mirror.c

NOMAN=

.include <bsd.kmod.mk>
