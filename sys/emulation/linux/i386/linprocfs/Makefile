# $FreeBSD: src/sys/modules/linprocfs/Makefile,v 1.1.2.3 2000/06/06 11:53:28 des Exp $
# $DragonFly: src/sys/emulation/linux/i386/linprocfs/Makefile,v 1.5 2004/08/13 02:59:29 dillon Exp $

.PATH:	${.CURDIR}/../../../../emulation/linux/i386/linprocfs
KMOD=	linprocfs
SRCS=	vnode_if.h linprocfs_misc.c linprocfs_subr.c \
		linprocfs_vfsops.c linprocfs_vnops.c
NOMAN=
CFLAGS+= -DLINPROCFS

.include <bsd.kmod.mk>
