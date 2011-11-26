# $FreeBSD: src/usr.sbin/ckdist/Makefile,v 1.2 1999/08/28 01:15:46 peter Exp $
# $DragonFly: src/usr.sbin/ckdist/Makefile,v 1.2 2003/06/17 04:29:52 dillon Exp $

.PATH: ${.CURDIR}/../../usr.bin/cksum

PROG=	ckdist
SRCS=   ckdist.c crc.c
WARNS?=	3

DPADD=	${LIBMD}
LDADD=	-lmd

.include <bsd.prog.mk>
