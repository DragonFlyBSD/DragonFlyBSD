# $FreeBSD: src/sys/modules/umapfs/Makefile,v 1.8 1999/12/12 16:47:33 peter Exp $
# $DragonFly: src/sys/vfs/umapfs/Attic/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

.PATH:	${.CURDIR}/../../miscfs/umapfs
KMOD=	umap
SRCS=	vnode_if.h \
	umap_subr.c umap_vfsops.c umap_vnops.c
NOMAN=

.include <bsd.kmod.mk>
