# $FreeBSD: src/sys/modules/union/Makefile,v 1.7 1999/12/12 20:34:11 peter Exp $
# $DragonFly: src/sys/vfs/union/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

.PATH:	${.CURDIR}/../../miscfs/union
KMOD=	union
SRCS=	vnode_if.h \
	union_subr.c union_vfsops.c union_vnops.c
NOMAN=

.include <bsd.kmod.mk>
