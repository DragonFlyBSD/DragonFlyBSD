# $FreeBSD: src/sys/modules/nullfs/Makefile,v 1.10 1999/12/12 16:47:29 peter Exp $
# $DragonFly: src/sys/vfs/nullfs/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:	${.CURDIR}/../../miscfs/nullfs
KMOD=	null
SRCS=	vnode_if.h \
	null_subr.c null_vfsops.c null_vnops.c
NOMAN=

.include <bsd.kmod.mk>
