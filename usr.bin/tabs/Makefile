# $FreeBSD: src/usr.bin/tabs/Makefile,v 1.1 2002/05/21 02:33:25 tjr Exp $

PROG=	tabs
DPADD=	${LIBTERMCAP}
LDADD=	-lprivate_termcap
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
