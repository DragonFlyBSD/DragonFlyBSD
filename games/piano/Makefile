#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/piano/Makefile,v 1.1.1.1.12.1 2001/04/25 09:28:59 ru Exp $
# $DragonFly: src/games/piano/Makefile,v 1.3 2005/05/31 00:22:38 swildner Exp $

PROG=	piano
MAN=	piano.6
DPADD=	${LIBCURSES} ${LIBTERMCAP}
LDADD=	-lcurses -ltermcap
WARNS?=	6

.include <bsd.prog.mk>
