# $FreeBSD: src/sys/modules/ntfs/Makefile,v 1.5 1999/12/12 20:34:07 peter Exp $

KMOD=	ntfs
SRCS=	ntfs_vfsops.c ntfs_vnops.c ntfs_subr.c ntfs_ihash.c \
	ntfs_compr.c

SRCS+=	opt_ntfs.h

SUBDIR= ntfs_iconv
.include <bsd.kmod.mk>
