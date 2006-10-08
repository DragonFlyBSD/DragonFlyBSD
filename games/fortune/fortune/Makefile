#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/fortune/fortune/Makefile,v 1.3.8.1 2001/04/25 09:28:54 ru Exp $
# $DragonFly: src/games/fortune/fortune/Makefile,v 1.4 2006/10/08 16:22:35 pavalos Exp $

PROG=	fortune
MAN=	fortune.6
CFLAGS+=-DDEBUG -I${.CURDIR}/../strfile
DPADD=	${LIBCOMPAT}
LDADD=	-lcompat

.include "${.CURDIR}/../../Makefile.inc"
.include <bsd.prog.mk>
