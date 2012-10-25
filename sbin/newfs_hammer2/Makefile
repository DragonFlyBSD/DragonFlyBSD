#
#
PROG=	newfs_hammer2
MAN=	newfs_hammer2.8
CFLAGS+= -I${.CURDIR}/../../sys -I${.CURDIR}/../hammer2
SRCS= newfs_hammer2.c

.PATH: ${.CURDIR}/../../sys/libkern
#.PATH: ${.CURDIR}/../../sys/vfs/hammer2
SRCS+= icrc32.c

.include <bsd.prog.mk>
