# $FreeBSD: src/sys/modules/nwfs/Makefile,v 1.5 1999/12/12 20:34:08 peter Exp $
# $DragonFly: src/sys/vfs/nwfs/Makefile,v 1.6 2005/09/11 10:52:56 swildner Exp $

KMOD=	nwfs

SRCS=	nwfs_node.c nwfs_ioctl.c nwfs_io.c nwfs_vfsops.c nwfs_vnops.c \
	nwfs_subr.c opt_ncp.h opt_nwfs.h opt_vmpage.h

.if defined(VNPRINT)
CFLAGS+= -DVNPRINT
.endif

opt_ncp.h:
	echo "#define NCP	1"  > ${.TARGET}

deinstall:
	rm -f ${DESTDIR}/modules/nwfs.ko

.include <bsd.kmod.mk>
