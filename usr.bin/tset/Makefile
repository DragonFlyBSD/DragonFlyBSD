#	@(#)Makefile	8.1 (Berkeley) 6/9/93

PROG=	tset
MLINKS=	tset.1 reset.1
LINKS=	${BINDIR}/tset ${BINDIR}/reset
SRCS=	map.c misc.c set.c term.c tset.c wrterm.c

DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
