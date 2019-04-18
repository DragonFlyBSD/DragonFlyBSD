# $FreeBSD: src/secure/usr.bin/scp/Makefile,v 1.2.2.3 2002/07/03 22:20:29 des Exp $
.PATH: ${.CURDIR}/../../crypto/openssh

PROG=	scp
SRCS=	scp.c
SRCS+=	progressmeter.c
WARNS?=	3

CFLAGS+=	-I${.CURDIR}/../../lib/libssh -I${.CURDIR}/../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}
LDADD=		-lprivate_ssh -lprivate_crypto -lutil -lz
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
