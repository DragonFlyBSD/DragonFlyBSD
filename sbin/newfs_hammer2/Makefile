#
#
PROG=	newfs_hammer2
MAN=	newfs_hammer2.8
CFLAGS+= -I${.CURDIR}/../../sys
cFLAGS+= -DXXH_NAMESPACE=h2_
SRCS= newfs_hammer2.c

.PATH: ${.CURDIR}/../../sys/libkern ${.CURDIR}/../../sys/vfs/hammer2/xxhash
SRCS+= icrc32.c xxhash.c

.include <bsd.prog.mk>
