#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/rain/Makefile,v 1.5.2.1 2001/04/25 09:29:01 ru Exp $

PROG=	rain
MAN=	rain.6
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
