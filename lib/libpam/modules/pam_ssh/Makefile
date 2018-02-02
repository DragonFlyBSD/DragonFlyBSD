LIB=	pam_ssh
SRCS=	pam_ssh.c
MAN=	pam_ssh.8
WARNS?=	4

LDADD=		-lprivate_ssh -lprivate_crypto -lcrypt
DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBCRYPT}

CFLAGS+=	-I${.CURDIR}/../../../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../../../crypto/libressl/include
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.lib.mk>
