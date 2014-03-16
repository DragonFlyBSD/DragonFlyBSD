TCPLAY_DIR=     ${.CURDIR}/../../lib/libtcplay

MAJ_VER=	2
MIN_VER=	0

PROG=   tcplay
MAN=	tcplay.8
WARNS?=	6

SRCS+=		tcplay.c crc32.c safe_mem.c io.c hdr.c humanize.c
SRCS+=		crypto.c generic_xts.c
SRCS+=		crypto-dev.c pbkdf2-openssl.c
SRCS+=		main.c

LDADD=	-lcrypto -ldm -lprop -lutil
DPADD=	${LIBCRYPTO} ${LIBDM} ${LIBPROP} ${LIBUTIL}

CFLAGS+=	-I${TCPLAY_DIR}
CFLAGS+=	-DMAJ_VER=${MAJ_VER} -DMIN_VER=${MIN_VER}
CFLAGS+=	-D_FILE_OFFSET_BITS=64

.PATH: ${TCPLAY_DIR}

.include <bsd.prog.mk>
