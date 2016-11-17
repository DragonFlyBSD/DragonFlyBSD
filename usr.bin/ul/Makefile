#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: head/usr.bin/ul/Makefile 87700 2001-12-11 23:27:23Z markm $

PROG=	ul

DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
