#
# $DragonFly: src/sys/vfs/hammer/Makefile,v 1.3 2007/11/07 00:43:24 dillon Exp $

KMOD=	hammer
SRCS=	hammer_vfsops.c hammer_vnops.c hammer_inode.c \
	hammer_subs.c hammer_ondisk.c hammer_io.c \
	hammer_btree.c hammer_transaction.c hammer_alist.c \
	hammer_object.c hammer_transaction.c hammer_spike.c

NOMAN=

.include <bsd.kmod.mk>
