DRIVER_DIR=	${.CURDIR}/../../driver

PROG=		dfregress2text
NOMAN=
DEBUG_FLAGS=	-O0 -g3

SRCS+=		fe_text.c config.c parser.c
SRCS+=		runlist.c testcase.c
SRCS+=		kernel.c userland.c

LDADD+=		-lprop -lutil
DPADD+=		${LIBPROP} ${LIBUITL}

CFLAGS+=	-I${DRIVER_DIR}

.PATH: ${DRIVER_DIR}

.include <bsd.prog.mk>
