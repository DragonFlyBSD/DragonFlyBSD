# $FreeBSD: src/sys/modules/msdos/Makefile,v 1.10.2.2 2002/12/01 09:01:08 nyan Exp $
# $DragonFly: src/sys/vfs/msdosfs/Makefile,v 1.2 2003/06/17 04:28:45 dillon Exp $

.PATH:	${.CURDIR}/../../msdosfs
KMOD=	msdos
SRCS=	vnode_if.h \
	msdosfs_conv.c msdosfs_denode.c msdosfs_fat.c msdosfs_lookup.c \
	msdosfs_vfsops.c msdosfs_vnops.c
NOMAN=

.include <bsd.kmod.mk>
