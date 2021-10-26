.PATH:	${.CURDIR}/msdos ${.CURDIR}/../../sbin/newfs_msdos

CFLAGS+=-I${.CURDIR}/../../sys
CFLAGS+=-I${.CURDIR}/../../sbin/newfs_msdos
CFLAGS+=-DMAKEFS

SRCS+=	msdosfs_vfsops.c msdosfs_vnops.c msdosfs_fat.c msdosfs_denode.c msdosfs_lookup.c msdosfs_conv.c
SRCS+=	mkfs_msdos.c
