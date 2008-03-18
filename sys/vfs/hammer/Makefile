#
# $DragonFly: src/sys/vfs/hammer/Makefile,v 1.9 2008/03/18 05:19:15 dillon Exp $

KMOD=	hammer
SRCS=	hammer_vfsops.c hammer_vnops.c hammer_inode.c \
	hammer_subs.c hammer_ondisk.c hammer_io.c \
	hammer_cursor.c hammer_btree.c hammer_transaction.c \
	hammer_object.c hammer_recover.c hammer_ioctl.c \
	hammer_blockmap.c hammer_freemap.c hammer_undo.c \
	hammer_reblock.c

NOMAN=

.include <bsd.kmod.mk>
