#
# $DragonFly: src/sbin/mount_hammer/Makefile,v 1.1 2007/10/10 19:35:19 dillon Exp $

PROG=	mount_hammer
SRCS=	mount_hammer.c getmntopts.c
MAN=	mount_hammer.8

MOUNT=	${.CURDIR}/../mount
CFLAGS+= -I${.CURDIR}/../../sys -I${MOUNT}
.PATH:	${MOUNT}

.include <bsd.prog.mk>
