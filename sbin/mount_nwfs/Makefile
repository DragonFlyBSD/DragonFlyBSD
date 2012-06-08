# $FreeBSD: src/sbin/mount_nwfs/Makefile,v 1.1.2.2 2001/04/25 10:58:39 ru Exp $

PROG=	mount_nwfs
MAN=	mount_nwfs.8

CFLAGS+= -DNWFS

DPADD=	${LIBNCP} ${LIBIPX} ${LIBUTIL}
LDADD=	-lncp -lipx -lutil

.include <bsd.prog.mk>
