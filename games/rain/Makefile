#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/rain/Makefile,v 1.5.2.1 2001/04/25 09:29:01 ru Exp $
# $DragonFly: src/games/rain/Makefile,v 1.3 2005/04/25 16:10:25 liamfoy Exp $

PROG=	rain
MAN=	rain.6
WARNS?=	6
DPADD=	${LIBCURSES} ${LIBTERMCAP}
LDADD=	-lcurses -ltermcap

.include <bsd.prog.mk>
