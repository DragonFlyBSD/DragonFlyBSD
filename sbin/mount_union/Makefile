#	@(#)Makefile	8.3 (Berkeley) 3/27/94
# $FreeBSD: src/sbin/mount_union/Makefile,v 1.6.6.1 2001/04/25 10:58:40 ru Exp $
# $DragonFly: src/sbin/mount_union/Makefile,v 1.3 2005/04/02 21:42:09 dillon Exp $

WARNS?= 6
PROG=	mount_union
SRCS=	mount_union.c getmntopts.c
MAN=	mount_union.8

MOUNT=	${.CURDIR}/../mount
CFLAGS+= -I${.CURDIR}/../../sys -I${MOUNT}
.PATH:	${MOUNT}

.include <bsd.prog.mk>
