# $FreeBSD: src/sys/modules/portal/Makefile,v 1.7 1999/12/12 20:34:09 peter Exp $
# $DragonFly: src/sys/vfs/portal/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:	${.CURDIR}/../../miscfs/portal
KMOD=	portal
SRCS=	vnode_if.h \
	portal_vfsops.c portal_vnops.c
NOMAN=

.include <bsd.kmod.mk>
