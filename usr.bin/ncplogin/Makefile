# $FreeBSD: src/usr.bin/ncplogin/Makefile,v 1.2.2.1 2001/04/25 11:29:29 ru Exp $
# $DragonFly: src/usr.bin/ncplogin/Makefile,v 1.2 2003/06/17 04:29:30 dillon Exp $

PROG=	ncplogin
MAN=	ncplogin.1 ncplogout.1

LINKS=	${BINDIR}/ncplogin ${BINDIR}/ncplogout

LDADD=	-lncp -lipx
DPADD=	${LIBNCP} ${LIBIPX}

.include <bsd.prog.mk>
