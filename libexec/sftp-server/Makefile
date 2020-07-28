# $FreeBSD: src/secure/libexec/sftp-server/Makefile,v 1.1.2.3 2002/07/03 22:13:22 des Exp $
.PATH: ${.CURDIR}/../../crypto/openssh

PROG=	sftp-server
SRCS=	sftp-server.c
SRCS+=	sftp-common.c sftp-realpath.c sftp-server-main.c
MAN=	sftp-server.8
WARNS?=	3

CFLAGS+=	-I${.CURDIR}/../../lib/libssh -I${.CURDIR}/../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}
LDADD=		-lprivate_ssh -lprivate_crypto -lutil -lz
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
