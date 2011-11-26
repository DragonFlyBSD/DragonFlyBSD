PROG=	mount_devfs
SRCS=	mount_devfs.c getmntopts.c
MAN=	mount_devfs.8

MOUNT=	${.CURDIR}/../mount
CFLAGS+= -I${.CURDIR}/../../sys -I${MOUNT}
.PATH:	${MOUNT}

.include <bsd.prog.mk>
