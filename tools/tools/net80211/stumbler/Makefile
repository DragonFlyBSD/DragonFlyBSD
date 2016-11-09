# $FreeBSD: src/tools/tools/net80211/stumbler/Makefile,v 1.2 2010/02/25 14:42:26 ru Exp $

PROG=	stumbler
BINDIR=	/usr/local/bin
DPADD=	${LIBPCAP} ${LIBNCURSES}
LDADD=	-lpcap -lprivate_ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
CFLAGS=-g
NOMAN=

.include <bsd.prog.mk>
