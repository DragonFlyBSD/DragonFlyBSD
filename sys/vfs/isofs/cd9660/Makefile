# $FreeBSD: src/sys/modules/cd9660/Makefile,v 1.9 1999/12/12 20:34:00 peter Exp $
# $DragonFly: src/sys/vfs/isofs/cd9660/Makefile,v 1.3 2004/08/13 17:51:11 dillon Exp $

.PATH:	${.CURDIR}/../../isofs/cd9660
KMOD=	cd9660
SRCS=	cd9660_bmap.c cd9660_lookup.c cd9660_node.c cd9660_rrip.c \
	cd9660_util.c cd9660_vfsops.c cd9660_vnops.c
NOMAN=

.include <bsd.kmod.mk>
