# $FreeBSD: src/sys/modules/msdos/Makefile,v 1.10.2.2 2002/12/01 09:01:08 nyan Exp $
# $DragonFly: src/sys/vfs/msdosfs/Makefile,v 1.5 2007/09/29 21:13:09 swildner Exp $

KMOD=	msdos
SRCS=	msdosfs_conv.c msdosfs_denode.c msdosfs_fat.c msdosfs_lookup.c \
	msdosfs_vfsops.c msdosfs_vnops.c opt_msdosfs.h
EXPORT_SYMS=    msdos_iconv

SUBDIR=msdosfs_iconv

.include <bsd.kmod.mk>
