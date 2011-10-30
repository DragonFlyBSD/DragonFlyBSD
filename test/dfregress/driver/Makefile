PROG=		dfregress
NOMAN=
DEBUG_FLAGS=	-O0 -g3
WARNS=		6

SRCS+=		main.c config.c parser.c
SRCS+=		runlist.c testcase.c
SRCS+=		kernel.c userland.c

LDADD+=		-lprop -lutil
DPADD+=		${LIBPROP} ${LIBUITL}

.include <bsd.prog.mk>
