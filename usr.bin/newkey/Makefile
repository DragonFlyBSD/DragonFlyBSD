# $FreeBSD: src/usr.bin/newkey/Makefile,v 1.3.2.1 2001/04/25 11:29:30 ru Exp $
# $DragonFly: src/usr.bin/newkey/Makefile,v 1.5 2007/08/27 16:50:57 pavalos Exp $

PROG= newkey
SRCS= newkey.c update.c generic.c
MAN= newkey.8

# Later
#CFLAGS+= -DYP

DPADD=	${LIBRPCSVC} ${LIBCRYPTO}
LDADD=	-lrpcsvc -lcrypto

.include <bsd.prog.mk>
