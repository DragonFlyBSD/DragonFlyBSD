# $FreeBSD: src/usr.bin/chkey/Makefile,v 1.3.2.1 2001/08/02 01:18:09 obrien Exp $
# $DragonFly: src/usr.bin/chkey/Makefile,v 1.4 2005/01/11 00:58:23 joerg Exp $

.PATH: ${.CURDIR}/../newkey

PROG=	chkey
SRCS=	chkey.c generic.c update.c
CFLAGS+=-DYP -I${.CURDIR}/../newkey
DPADD=	${LIBRPCSVC} ${LIBCRYPTO}
LDADD=	-lrpcsvc -lcrypto
WARNS?=	6

.include <bsd.prog.mk>
