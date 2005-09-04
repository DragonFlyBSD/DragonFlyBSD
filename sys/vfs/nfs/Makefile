# $FreeBSD: src/sys/modules/nfs/Makefile,v 1.16 1999/12/12 20:34:06 peter Exp $
# $DragonFly: src/sys/vfs/nfs/Makefile,v 1.5 2005/09/04 04:06:28 dillon Exp $

KMOD=	nfs
SRCS=	nfs_bio.c nfs_node.c nfs_nqlease.c nfs_serv.c nfs_socket.c \
	nfs_srvcache.c nfs_subs.c nfs_syscalls.c nfs_vfsops.c \
	nfs_vnops.c opt_inet.h opt_nfs.h opt_vmpage.h opt_bootp.h \
	opt_nfsroot.h
NFS_INET?=	1	# 0/1 - requires INET to be configured in kernel
NOMAN=

opt_inet.h:
	touch ${.TARGET}
.if ${NFS_INET} > 0
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
