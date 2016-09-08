CRYPTSETUP_DIR=	${.CURDIR}/../../../contrib/cryptsetup

LIB=		luks
SHLIB_MAJOR?=	0
WARNS?=		1

SRCS+=		af.c hexprint.c keyencryption.c keymanage.c pbkdf.c random.c
INCS+=		luks.h

CFLAGS+=	-I${CRYPTSETUP_DIR}/luks
CFLAGS+=	-I${.CURDIR}/../../../crypto/libressl/include

.PATH: ${CRYPTSETUP_DIR}/luks

.include <bsd.lib.mk>
