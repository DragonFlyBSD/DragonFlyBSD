#
# $FreeBSD: src/sbin/mount_hpfs/Makefile,v 1.1.2.1 2001/04/25 10:58:35 ru Exp $
# $DragonFly: src/sbin/mount_hpfs/Makefile,v 1.4 2006/10/17 00:55:42 pavalos Exp $
#

PROG=	mount_hpfs
SRCS=	mount_hpfs.c getmntopts.c
MAN=	mount_hpfs.8

MOUNT=	${.CURDIR}/../mount
CFLAGS+=-I${.CURDIR}/../../sys -I${MOUNT}
.PATH:	${MOUNT}

.include <bsd.prog.mk>
