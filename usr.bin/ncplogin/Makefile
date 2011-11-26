# $FreeBSD: src/usr.bin/ncplogin/Makefile,v 1.2.2.1 2001/04/25 11:29:29 ru Exp $
# $DragonFly: src/usr.bin/ncplogin/Makefile,v 1.3 2007/08/27 16:50:56 pavalos Exp $

PROG=	ncplogin
MAN=	ncplogin.1 ncplogout.1
WARNS?=	3

LINKS=	${BINDIR}/ncplogin ${BINDIR}/ncplogout

LDADD=	-lncp -lipx
DPADD=	${LIBNCP} ${LIBIPX}

.include <bsd.prog.mk>
