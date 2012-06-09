#	@(#)Makefile	8.6 (Berkeley) 5/8/95
# $FreeBSD: src/sbin/mount/Makefile,v 1.9.2.1 2001/04/25 10:58:33 ru Exp $

PROG=	mount_ufs
SRCS=	mount.c mount_ufs.c vfslist.c
MAN=	mount_ufs.8

MOUNT=  ${.CURDIR}/../mount
CFLAGS+=-I${MOUNT}

LDADD=	-lutil
DPADD=	${LIBUTIL}

.PATH:  ${MOUNT}

.include <bsd.prog.mk>
