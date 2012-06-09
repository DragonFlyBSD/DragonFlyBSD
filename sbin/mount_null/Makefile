#	@(#)Makefile	8.3 (Berkeley) 3/27/94
# $FreeBSD: src/sbin/mount_null/Makefile,v 1.5.6.1 2001/04/25 10:58:38 ru Exp $

PROG=	mount_null
MAN=	mount_null.8

LDADD=	-lutil
DPADD=	${LIBUTIL}

CFLAGS+=-I${.CURDIR}/../../sys

.include <bsd.prog.mk>
