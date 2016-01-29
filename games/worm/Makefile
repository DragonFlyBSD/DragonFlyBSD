#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/worm/Makefile,v 1.2.14.1 2001/04/25 09:29:04 ru Exp $

PROG=	worm
MAN=	worm.6
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv
HIDEGAME=hidegame

.include <bsd.prog.mk>
