TCPLAY_DIR=     ${.CURDIR}/../../lib/libtcplay

PROG=   tcplay
MAN=	tcplay.8
WARNS?=	6

SRCS=	main.c
SRCS+=	tcplay.c crc32.c safe_mem.c io.c crypto-dev.c hdr.c

LDADD=	-lcrypto -ldm -lprop -lutil
DPADD=	${LIBCRYPTO} ${LIBDM} ${LIBPROP} ${LIBUTIL}

CFLAGS+=	-I${TCPLAY_DIR}

.PATH: ${TCPLAY_DIR}

.include <bsd.prog.mk>
