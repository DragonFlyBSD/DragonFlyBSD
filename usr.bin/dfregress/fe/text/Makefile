DRIVER_DIR=	${.CURDIR}/../..

PROG=		dfr2text
MAN=		dfr2text.8

SRCS+=		fe_text.c config.c parser.c
SRCS+=		runlist.c testcase.c
SRCS+=		kernel.c userland.c

LDADD+=		-lprop -lutil
DPADD+=		${LIBPROP} ${LIBUTIL}

CFLAGS+=	-I${DRIVER_DIR}

.PATH: ${DRIVER_DIR}

.include <bsd.prog.mk>
