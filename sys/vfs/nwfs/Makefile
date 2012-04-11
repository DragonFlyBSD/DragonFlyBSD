# $FreeBSD: src/sys/modules/nwfs/Makefile,v 1.5 1999/12/12 20:34:08 peter Exp $

KMOD=	nwfs

SRCS=	nwfs_node.c nwfs_ioctl.c nwfs_io.c nwfs_vfsops.c nwfs_vnops.c \
	nwfs_subr.c opt_ncp.h

.if !defined(BUILDING_WITH_KERNEL)
opt_ncp.h:
	echo "#define NCP	1"  > ${.TARGET}
.endif

.include <bsd.kmod.mk>
