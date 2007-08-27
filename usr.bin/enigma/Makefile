# $FreeBSD: src/usr.bin/enigma/Makefile,v 1.3.6.3 2001/07/30 09:59:16 dd Exp $
# $DragonFly: src/usr.bin/enigma/Makefile,v 1.5 2007/08/27 16:50:53 pavalos Exp $

PROG=	enigma

LINKS=	${BINDIR}/enigma ${BINDIR}/crypt
MLINKS=	enigma.1 crypt.1

DPADD+=	${LIBCRYPT}
LDADD+=	-lcrypt

.include <bsd.prog.mk>
