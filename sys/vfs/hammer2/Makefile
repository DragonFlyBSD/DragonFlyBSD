# Makefile for hammer2 vfs
#
#
.PATH: ${.CURDIR}

CFLAGS+= -DINVARIANTS -DSMP
KMOD=	hammer2
SRCS=	hammer2_vfsops.c hammer2_vnops.c hammer2_inode.c hammer2_ccms.c
SRCS+=	hammer2_chain.c hammer2_freemap.c hammer2_subr.c hammer2_icrc.c
SRCS+=	hammer2_ioctl.c hammer2_msg.c

.include <bsd.kmod.mk>
