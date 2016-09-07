LIB=	pam_ssh
SRCS=	pam_ssh.c
MAN=	pam_ssh.8
WARNS?=	4

LDADD=		-lprivate_ssh -lprivate_crypto -lcrypt
DPADD=		${LIBSSH} ${LIBRECRYPTO} ${LIBCRYPT}

CFLAGS+=	-I${.CURDIR}/../../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../../crypto/libressl/include
LDFLAGS+=	-rpath /lib/priv:/usr/lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.lib.mk>
