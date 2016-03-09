# $FreeBSD: src/usr.bin/tabs/Makefile,v 1.1 2002/05/21 02:33:25 tjr Exp $

PROG=	tabs
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
