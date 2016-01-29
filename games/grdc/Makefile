# $FreeBSD: src/games/grdc/Makefile,v 1.4.2.2 2001/10/02 11:51:49 ru Exp $

PROG=	grdc
MAN=	grdc.6
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
