#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $DragonFly: src/usr.bin/quota/Makefile,v 1.2 2005/03/18 00:57:10 joerg Exp $

PROG=	quota
BINMODE=4555
WARNS?=	6

DPADD=	${LIBRPCSVC}
LDADD=	-lrpcsvc

.include <bsd.prog.mk>
