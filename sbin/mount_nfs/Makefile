#	@(#)Makefile	8.2 (Berkeley) 3/27/94
#
# $FreeBSD: src/sbin/mount_nfs/Makefile,v 1.8.2.1 2001/04/25 10:58:37 ru Exp $
# $DragonFly: src/sbin/mount_nfs/Makefile,v 1.5 2006/10/17 00:55:42 pavalos Exp $

PROG=	mount_nfs
SRCS=	mount_nfs.c getmntopts.c mounttab.c
MAN=	mount_nfs.8

MOUNT=	${.CURDIR}/../mount
UMNTALL= ${.CURDIR}/../../usr.sbin/rpc.umntall
CFLAGS+= -DNFS -I${MOUNT} -I${UMNTALL}
.PATH: ${MOUNT} ${UMNTALL}

.include <bsd.prog.mk>
