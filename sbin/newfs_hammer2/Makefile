#
#
PROG=	newfs_hammer2
MAN=	newfs_hammer2.8
CFLAGS+= -I${.CURDIR}/../../sys -I${.CURDIR}/../hammer2
cFLAGS+= -DXXH_NAMESPACE=h2_
SRCS= newfs_hammer2.c

.PATH: ${.CURDIR}/../../sys/libkern ${.CURDIR}/../../sys/vfs/hammer2/xxhash
#.PATH: ${.CURDIR}/../../sys/vfs/hammer2
SRCS+= icrc32.c xxhash.c

.include <bsd.prog.mk>
