# $FreeBSD: src/sys/modules/mfs/Makefile,v 1.8 1999/12/12 16:47:26 peter Exp $
# $DragonFly: src/sys/vfs/mfs/Makefile,v 1.2 2003/06/17 04:28:44 dillon Exp $

.PATH:	${.CURDIR}/../../ufs/mfs
KMOD=	mfs
SRCS=	vnode_if.h \
	mfs_vfsops.c mfs_vnops.c opt_mfs.h
NOMAN=

.include <bsd.kmod.mk>
