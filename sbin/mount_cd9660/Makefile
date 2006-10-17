#	@(#)Makefile	8.3 (Berkeley) 3/27/94
# $FreeBSD: src/sbin/mount_cd9660/Makefile,v 1.4.6.1 2001/04/25 10:58:34 ru Exp $
# $DragonFly: src/sbin/mount_cd9660/Makefile,v 1.4 2006/10/17 00:55:42 pavalos Exp $

PROG=	mount_cd9660
SRCS=	mount_cd9660.c getmntopts.c
MAN=	mount_cd9660.8

MOUNT=	${.CURDIR}/../mount
CFLAGS+= -I${MOUNT}
.PATH:	${MOUNT}

.include <bsd.prog.mk>
