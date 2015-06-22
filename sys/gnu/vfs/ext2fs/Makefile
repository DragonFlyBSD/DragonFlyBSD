# $FreeBSD: src/sys/modules/ext2fs/Makefile,v 1.4.2.1 2002/08/06 14:14:25 mdodd Exp $

KMOD=	ext2fs
SRCS=	opt_quota.h opt_suiddir.h \
	ext2_alloc.c ext2_balloc.c ext2_inode.c ext2_inode_cnv.c \
	ext2_linux_balloc.c ext2_linux_ialloc.c ext2_lookup.c \
	ext2_subr.c ext2_vfsops.c ext2_vnops.c ext2_bmap.c \
	ext2_ihash.c ext2_quota.c ext2_linux_super.c

.include <bsd.kmod.mk>
