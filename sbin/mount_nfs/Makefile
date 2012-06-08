#	@(#)Makefile	8.2 (Berkeley) 3/27/94
#
# $FreeBSD: src/sbin/mount_nfs/Makefile,v 1.8.2.1 2001/04/25 10:58:37 ru Exp $

PROG=	mount_nfs
SRCS=	mount_nfs.c mounttab.c
MAN=	mount_nfs.8

LDADD=	-lutil
DPADD=	${LIBUTIL}

UMNTALL= ${.CURDIR}/../../usr.sbin/rpc.umntall
CFLAGS+= -DNFS -I${UMNTALL}
.PATH: ${UMNTALL}

.include <bsd.prog.mk>
