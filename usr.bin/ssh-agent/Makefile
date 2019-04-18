# $FreeBSD: src/secure/usr.bin/ssh-agent/Makefile,v 1.2.2.3 2002/07/03 22:20:31 des Exp $
.PATH: ${.CURDIR}/../../crypto/openssh

PROG=	ssh-agent
SRCS=	ssh-agent.c
WARNS?=	3

# not needed: ssh-pkcs11-client.c

CFLAGS+=	-I${.CURDIR}/../../lib/libssh -I${.CURDIR}/../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}
LDADD=		-lprivate_ssh -lprivate_crypto -lutil -lz
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
