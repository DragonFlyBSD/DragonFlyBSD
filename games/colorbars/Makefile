# $NetBSD: Makefile,v 1.4 2013/12/07 02:24:12 dholland Exp $

PROG=  colorbars
MAN=   colorbars.6
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
