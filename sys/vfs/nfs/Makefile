# $FreeBSD: src/sys/modules/nfs/Makefile,v 1.16 1999/12/12 20:34:06 peter Exp $
# $DragonFly: src/sys/vfs/nfs/Makefile,v 1.6 2006/03/27 16:18:39 dillon Exp $

KMOD=	nfs
SRCS=	nfs_bio.c nfs_node.c nfs_kerb.c nfs_serv.c nfs_socket.c \
	nfs_srvcache.c nfs_subs.c nfs_syscalls.c nfs_vfsops.c nfs_iod.c \
	nfsm_subs.c nfs_vnops.c \
	opt_inet.h opt_nfs.h opt_bootp.h opt_nfsroot.h

# 0/1 - requires INET to be configured in kernel
#
NFS_INET?=	1

opt_inet.h:
	touch ${.TARGET}
.if ${NFS_INET} > 0
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
