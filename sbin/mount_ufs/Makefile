#	@(#)Makefile	8.6 (Berkeley) 5/8/95
# $FreeBSD: src/sbin/mount/Makefile,v 1.9.2.1 2001/04/25 10:58:33 ru Exp $
# $DragonFly: src/sbin/mount/Makefile,v 1.5 2006/10/28 20:42:54 pavalos Exp $

PROG=	mount_ufs
SRCS=	mount.c mount_ufs.c getmntopts.c vfslist.c
MAN=	mount_ufs.8

MOUNT=  ${.CURDIR}/../mount
CFLAGS+= -I${.CURDIR}/../../sys -I${MOUNT}

.PATH:  ${MOUNT}

.include <bsd.prog.mk>
