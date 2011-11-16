PROG=		dfregress
INCS=		dfregress.h
NOMAN=

CFLAGS+=	-I.

SRCS+=		main.c config.c parser.c
SRCS+=		runlist.c testcase.c
SRCS+=		kernel.c userland.c

LDADD+=		-lprop -lutil
DPADD+=		${LIBPROP} ${LIBUITL}

SUBDIR+=	fe
.include <bsd.prog.mk>
