# $FreeBSD: src/sys/modules/ext2fs/Makefile,v 1.4.2.1 2002/08/06 14:14:25 mdodd Exp $
# $DragonFly: src/sys/vfs/gnu/ext2fs/Makefile,v 1.4 2006/04/04 17:34:32 dillon Exp $

.PATH:	${.CURDIR}/../../gnu/ext2fs
KMOD=	ext2fs
SRCS=	opt_ddb.h opt_quota.h opt_suiddir.h \
	ext2_alloc.c ext2_balloc.c ext2_inode.c ext2_inode_cnv.c \
	ext2_linux_balloc.c ext2_linux_ialloc.c ext2_lookup.c \
	ext2_subr.c ext2_vfsops.c ext2_vnops.c ext2_bmap.c \
	ext2_ihash.c ext2_quota.c


.include <bsd.kmod.mk>
