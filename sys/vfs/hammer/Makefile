#
# $DragonFly: src/sys/vfs/hammer/Makefile,v 1.7 2008/02/08 08:30:59 dillon Exp $

KMOD=	hammer
SRCS=	hammer_vfsops.c hammer_vnops.c hammer_inode.c \
	hammer_subs.c hammer_ondisk.c hammer_io.c \
	hammer_cursor.c hammer_btree.c hammer_transaction.c \
	hammer_object.c hammer_spike.c \
	hammer_recover.c hammer_ioctl.c

NOMAN=

.include <bsd.kmod.mk>
