#
# $DragonFly: src/sys/vfs/hammer/Makefile,v 1.5 2008/01/09 00:46:22 dillon Exp $

KMOD=	hammer
SRCS=	hammer_vfsops.c hammer_vnops.c hammer_inode.c \
	hammer_subs.c hammer_ondisk.c hammer_io.c \
	hammer_cursor.c hammer_btree.c hammer_transaction.c \
	hammer_alist.c hammer_object.c hammer_spike.c \
	hammer_recover.c

NOMAN=

.include <bsd.kmod.mk>
