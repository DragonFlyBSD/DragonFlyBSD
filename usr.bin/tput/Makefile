#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/tput/Makefile,v 1.3.6.1 2001/04/25 14:04:33 ru Exp $

PROG=	tput
MLINKS=	tput.1 clear.1
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
SCRIPTS=	clear.sh

.include <bsd.prog.mk>
