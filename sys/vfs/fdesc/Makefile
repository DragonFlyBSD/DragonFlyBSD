# $FreeBSD: src/sys/modules/fdesc/Makefile,v 1.6 1999/12/12 16:47:24 peter Exp $
# $DragonFly: src/sys/vfs/fdesc/Makefile,v 1.3 2003/08/15 07:26:14 dillon Exp $

KMOD=	fdesc
SRCS=	vnode_if.h \
	fdesc_vfsops.c fdesc_vnops.c
NOMAN=
CFLAGS+= -DFDESC

.include <bsd.kmod.mk>
