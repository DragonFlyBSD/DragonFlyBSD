# $FreeBSD: src/usr.sbin/watch/Makefile,v 1.8.2.2 2002/03/11 01:26:42 dd Exp $

PROG=	watch
MAN=	watch.8

LDADD=	-lprivate_ncurses
DPADD=	${LIBNCURSES}

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
