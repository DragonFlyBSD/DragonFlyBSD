# $FreeBSD: src/usr.bin/newkey/Makefile,v 1.3.2.1 2001/04/25 11:29:30 ru Exp $
# $DragonFly: src/usr.bin/newkey/Makefile,v 1.4 2005/01/11 00:58:23 joerg Exp $

PROG= newkey
SRCS= newkey.c update.c generic.c
MAN= newkey.8

# Later
#CFLAGS+= -DYP

DPADD=	${LIBRPCSVC} ${LIBCRYPTO}
LDADD=	-lrpcsvc -lcrypto
WARNS?=	6

.include <bsd.prog.mk>
