# $FreeBSD: head/usr.sbin/ckdist/Makefile 314659 2017-03-04 11:38:03Z ngie $

.PATH: ${.CURDIR}/../../usr.bin/cksum

PROG=	ckdist
SRCS=   ckdist.c crc.c

DPADD=	${LIBMD}
LDADD=	-lmd

# XXX sys/md5.h shim errata for bootstrap REMOVE_OPENSSL_FILES
CFLAGS+= -I${_SHLIBDIRPREFIX}/usr/include/priv

.include <bsd.prog.mk>
