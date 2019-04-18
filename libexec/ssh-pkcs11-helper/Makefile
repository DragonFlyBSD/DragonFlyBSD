.PATH: ${.CURDIR}/../../crypto/openssh

PROG=	ssh-pkcs11-helper
SRCS=	ssh-pkcs11-helper.c
# disabled:
#SRCS+=	ssh-pkcs11.c
MAN=	ssh-pkcs11-helper.8

WARNS=	3

.PATH: ${.CURDIR}/../../crypto/openssh

CFLAGS+=	-I${.CURDIR}/../../lib/libssh -I${.CURDIR}/../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}
LDADD=		-lprivate_ssh -lprivate_crypto -lutil -lz
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
