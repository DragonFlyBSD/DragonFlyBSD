CRYPTSETUP_DIR=	${.CURDIR}/../../../contrib/cryptsetup

LIB=		cryptsetup
SHLIB_MAJOR?=	0
WARNS?=		1

SRCS+=		backends.c libdevmapper.c setup.c utils.c \
		utils_debug.c
INCS+=		libcryptsetup.h

CFLAGS+=	-I${CRYPTSETUP_DIR}/lib
CFLAGS+=	-I${.CURDIR}/../../../crypto/libressl/include

.PATH: ${CRYPTSETUP_DIR}/lib

.include <bsd.lib.mk>
