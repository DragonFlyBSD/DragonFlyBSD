TCPLAY_DIR=     ${.CURDIR}/../../lib/libtcplay

MAJ_VER=	3
MIN_VER=	3

PROG=   tcplay
MAN=	tcplay.8

SRCS+=		tcplay.c crc32.c safe_mem.c io.c hdr.c humanize.c
SRCS+=		crypto.c generic_xts.c
SRCS+=		pbkdf2-openssl.c
SRCS+=		main.c

SRCS+=		crypto-api.c

.PATH:		${TCPLAY_DIR}/../../sys/crypto/cryptoapi
SRCS+=		cryptoapi.c
.PATH:		${TCPLAY_DIR}/../../sys/crypto/rijndael
SRCS+=		rijndael-alg-fst.c rijndael-api.c
.PATH:		${TCPLAY_DIR}/../../sys/crypto/serpent
SRCS+=		serpent.c
.PATH:		${TCPLAY_DIR}/../../sys/crypto/twofish
SRCS+=		twofish.c


LDADD=		-ldm -lprop -lutil
DPADD=		${LIBDM} ${LIBPROP} ${LIBUTIL}

DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
CFLAGS+=	-I${.CURDIR}/../../sys

CFLAGS+=	-I${TCPLAY_DIR}
CFLAGS+=	-DMAJ_VER=${MAJ_VER} -DMIN_VER=${MIN_VER}
CFLAGS+=	-D_FILE_OFFSET_BITS=64

.PATH: ${TCPLAY_DIR}

.include <bsd.prog.mk>
