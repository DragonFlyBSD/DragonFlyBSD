#	@(#)Makefile	8.3 (Berkeley) 3/27/94
# $FreeBSD: src/sbin/mount_union/Makefile,v 1.6.6.1 2001/04/25 10:58:40 ru Exp $

PROG=	mount_union
MAN=	mount_union.8

LDADD=	-lutil
DPADD=	${LIBUTIL}

CFLAGS+=-I${.CURDIR}/../../sys

.include <bsd.prog.mk>
