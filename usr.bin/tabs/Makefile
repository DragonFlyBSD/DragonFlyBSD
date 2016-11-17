# $FreeBSD: src/usr.bin/tabs/Makefile,v 1.1 2002/05/21 02:33:25 tjr Exp $

PROG=	tabs
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
