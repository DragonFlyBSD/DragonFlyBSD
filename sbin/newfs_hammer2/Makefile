PROG=	newfs_hammer2
SRCS=	newfs_hammer2.c
MAN=	newfs_hammer2.8

CFLAGS+= -I${.CURDIR}/../../sys -I${.CURDIR}/../hammer2
CFLAGS+= -DXXH_NAMESPACE=h2_

.PATH: ${.CURDIR}/../../sys/libkern ${.CURDIR}/../../sys/vfs/hammer2/xxhash
SRCS+= icrc32.c xxhash.c
.PATH: ${.CURDIR}/../hammer2
SRCS+= subs.c

.include <bsd.prog.mk>
