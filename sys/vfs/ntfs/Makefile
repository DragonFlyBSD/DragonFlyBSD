# $FreeBSD: src/sys/modules/ntfs/Makefile,v 1.5 1999/12/12 20:34:07 peter Exp $
# $DragonFly: src/sys/vfs/ntfs/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:	${.CURDIR}/../../ntfs
KMOD=	ntfs
SRCS=	vnode_if.h \
	ntfs_vfsops.c ntfs_vnops.c ntfs_subr.c ntfs_ihash.c \
	ntfs_compr.c
NOMAN=

.include <bsd.kmod.mk>
