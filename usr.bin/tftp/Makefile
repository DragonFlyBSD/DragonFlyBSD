#	@(#)Makefile	8.1 (Berkeley) 6/6/93

PROG=	tftp
SRCS=	main.c tftp.c tftpsubs.c

NO_WARRAY_BOUNDS=

CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD+=		${LIBEDIT} ${LIBNCURSES}
LDADD+=		-lprivate_edit -lprivate_ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
