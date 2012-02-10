# Makefile for hammer2 vfs
#
#
.PATH: ${.CURDIR}

KMOD=	hammer2
SRCS=	hammer2_vfsops.c hammer2_vnops.c hammer2_inode.c
SRCS+=	hammer2_chain.c hammer2_subr.c hammer2_icrc.c

.include <bsd.kmod.mk>
