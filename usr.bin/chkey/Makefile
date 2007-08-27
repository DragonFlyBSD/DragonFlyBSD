# $FreeBSD: src/usr.bin/chkey/Makefile,v 1.3.2.1 2001/08/02 01:18:09 obrien Exp $
# $DragonFly: src/usr.bin/chkey/Makefile,v 1.5 2007/08/27 16:50:52 pavalos Exp $

.PATH: ${.CURDIR}/../newkey

PROG=	chkey
SRCS=	chkey.c generic.c update.c
CFLAGS+=-DYP -I${.CURDIR}/../newkey
DPADD=	${LIBRPCSVC} ${LIBCRYPTO}
LDADD=	-lrpcsvc -lcrypto

.include <bsd.prog.mk>
