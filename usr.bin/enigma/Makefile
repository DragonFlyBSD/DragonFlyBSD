# $FreeBSD: src/usr.bin/enigma/Makefile,v 1.3.6.3 2001/07/30 09:59:16 dd Exp $
# $DragonFly: src/usr.bin/enigma/Makefile,v 1.4 2005/01/05 18:42:33 liamfoy Exp $

PROG=	enigma
WARNS?= 6	

LINKS=	${BINDIR}/enigma ${BINDIR}/crypt
MLINKS=	enigma.1 crypt.1

DPADD+=	${LIBCRYPT}
LDADD+=	-lcrypt

.include <bsd.prog.mk>
