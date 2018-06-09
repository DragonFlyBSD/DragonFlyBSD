CRYPTSETUP_DIR=		${.CURDIR}/../../../contrib/cryptsetup

PROG=		cryptsetup

.PATH:		${CRYPTSETUP_DIR}/src
CFLAGS+=	-I${CRYPTSETUP_DIR}/src
SRCS=		cryptsetup.c

.PATH:		${CRYPTSETUP_DIR}/luks
CFLAGS+=	-I${CRYPTSETUP_DIR}/luks
SRCS+=		af.c hexprint.c keyencryption.c keymanage.c \
		pbkdf.c random.c

.PATH:		${CRYPTSETUP_DIR}/lib
CFLAGS+=	-I${CRYPTSETUP_DIR}/lib
SRCS+=		backends.c libdevmapper.c setup.c utils.c \
		utils_debug.c

.PATH:		${CRYPTSETUP_DIR}/man
MAN=		cryptsetup.8

CFLAGS+=	-I. -I${CRYPTSETUP_DIR} -D_BSD_SOURCE

DPADD+=		${LIBDM} ${LIBPROP}
LDADD+=		-ldm -lprop

DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
CFLAGS+=	-I${.CURDIR}/../../../crypto/libressl/include

.include <bsd.prog.mk>
