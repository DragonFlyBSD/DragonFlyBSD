PROG=		dfregress
INCS=		dfregress.h
MAN=		dfregress.8

CFLAGS+=	-I.

SRCS+=		main.c config.c parser.c
SRCS+=		runlist.c testcase.c
SRCS+=		kernel.c userland.c

LDADD+=		-lprop -lutil
DPADD+=		${LIBPROP} ${LIBUTIL}

SUBDIR+=	fe
.include <bsd.prog.mk>
