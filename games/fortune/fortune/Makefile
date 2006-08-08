#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/fortune/fortune/Makefile,v 1.3.8.1 2001/04/25 09:28:54 ru Exp $
# $DragonFly: src/games/fortune/fortune/Makefile,v 1.3 2006/08/08 16:58:59 pavalos Exp $

PROG=	fortune
MAN=	fortune.6
CFLAGS+=-DDEBUG -Wall -I${.CURDIR}/../strfile
DPADD=	${LIBCOMPAT}
LDADD=	-lcompat
WARNS?=	6

.include "${.CURDIR}/../../Makefile.inc"
.include <bsd.prog.mk>
