# $FreeBSD: src/sys/modules/fdesc/Makefile,v 1.6 1999/12/12 16:47:24 peter Exp $
# $DragonFly: src/sys/vfs/fdesc/Makefile,v 1.2 2003/06/17 04:28:44 dillon Exp $

.PATH:	${.CURDIR}/../../miscfs/fdesc
KMOD=	fdesc
SRCS=	vnode_if.h \
	fdesc_vfsops.c fdesc_vnops.c
NOMAN=
CFLAGS+= -DFDESC

.include <bsd.kmod.mk>
