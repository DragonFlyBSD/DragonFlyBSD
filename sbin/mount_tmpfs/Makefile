#
# $DragonFly: src/sbin/mount_hammer/Makefile,v 1.1 2007/10/10 19:35:19 dillon Exp $

PROG=	mount_tmpfs
SRCS=	mount_tmpfs.c fscopy.c getmntopts.c
MAN=	mount_tmpfs.8
LDADD+= -lutil

MOUNT=	${.CURDIR}/../mount
CFLAGS+=-I${.CURDIR}/../../sys -I${MOUNT}
.PATH:	${MOUNT}

NEWFS=	${.CURDIR}/../newfs
CFLAGS+=-I${NEWFS}
.PATH:	${NEWFS}

.include <bsd.prog.mk>
