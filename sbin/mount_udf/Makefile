# $DragonFly: src/sbin/mount_udf/Makefile,v 1.2 2006/10/17 00:55:42 pavalos Exp $

PROG=	mount_udf
SRCS=	mount_udf.c getmntopts.c
MAN=	mount_udf.8

MOUNT=	${.CURDIR}/../mount
CFLAGS+= -I${MOUNT}
.PATH:	${MOUNT}

.include <bsd.prog.mk>
