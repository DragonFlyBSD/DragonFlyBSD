# $FreeBSD: src/usr.sbin/ckdist/Makefile,v 1.2 1999/08/28 01:15:46 peter Exp $

.PATH: ${.CURDIR}/../../usr.bin/cksum

PROG=	ckdist
SRCS=   ckdist.c crc.c
WARNS?=	3

DPADD=	${LIBMD}
LDADD=	-lmd

# XXX sys/md5.h shim errata for bootstrap REMOVE_OPENSSL_FILES
CFLAGS+= -I${_SHLIBDIRPREFIX}/usr/include/priv

.include <bsd.prog.mk>
