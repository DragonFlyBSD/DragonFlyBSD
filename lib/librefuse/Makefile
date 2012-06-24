# $NetBSD: Makefile,v 1.8 2007/11/05 13:41:52 pooka Exp $

LIB=		refuse

SRCS=		refuse.c refuse_opt.c
MAN=		refuse.3
WARNS=		2
INCS=		fuse.h fuse_opt.h

MLINKS=		refuse.3 fuse_main.3 \
		refuse.3 fuse_opt_add_arg.3 \
		refuse.3 fuse_opt_parse.3 \
		refuse.3 fuse_setup.3 \
		refuse.3 fuse_teardown.3 \
		refuse.3 puffs_fuse_node_create.3 \
		refuse.3 puffs_fuse_node_getattr.3 \
		refuse.3 puffs_fuse_node_link.3 \
		refuse.3 puffs_fuse_node_mkdir.3 \
		refuse.3 puffs_fuse_node_mknod.3 \
		refuse.3 puffs_fuse_node_open.3 \
		refuse.3 puffs_fuse_node_read.3 \
		refuse.3 puffs_fuse_node_readdir.3 \
		refuse.3 puffs_fuse_node_readlink.3 \
		refuse.3 puffs_fuse_node_rename.3 \
		refuse.3 puffs_fuse_node_rmdir.3 \
		refuse.3 puffs_fuse_node_symlink.3 \
		refuse.3 puffs_fuse_node_write.3

.ifdef DEBUG
CFLAGS+=	-g -DFUSE_OPT_DEBUG
.endif

.include <bsd.lib.mk>
