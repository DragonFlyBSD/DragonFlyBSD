#	@(#)Makefile	8.3 (Berkeley) 3/27/94
# $FreeBSD: src/sbin/mount_ext2fs/Makefile,v 1.3.6.1 2001/04/25 10:58:34 ru Exp $
# $DragonFly: src/sbin/mount_ext2fs/Makefile,v 1.2 2003/06/17 04:27:33 dillon Exp $

PROG=	mount_ext2fs
SRCS=	mount_ext2fs.c getmntopts.c
MAN=	mount_ext2fs.8

MOUNT=	${.CURDIR}/../mount
CFLAGS+= -I${MOUNT}
.PATH:	${MOUNT}

.include <bsd.prog.mk>
