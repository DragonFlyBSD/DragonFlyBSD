# $FreeBSD: src/sys/modules/hpfs/Makefile,v 1.3 1999/12/12 20:34:02 peter Exp $
# $DragonFly: src/sys/vfs/hpfs/Makefile,v 1.3 2003/08/15 07:26:15 dillon Exp $

KMOD=	hpfs
SRCS=	vnode_if.h \
	hpfs_vfsops.c hpfs_vnops.c hpfs_hash.c hpfs_subr.c hpfs_lookup.c \
	hpfs_alsubr.c
NOMAN=

.include <bsd.kmod.mk>
