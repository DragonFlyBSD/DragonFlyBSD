# $FreeBSD: src/usr.bin/chkey/Makefile,v 1.3.2.1 2001/08/02 01:18:09 obrien Exp $
# $DragonFly: src/usr.bin/chkey/Makefile,v 1.2 2003/06/17 04:29:25 dillon Exp $

.PATH: ${.CURDIR}/../newkey

PROG=	chkey
SRCS=	chkey.c generic.c update.c
CFLAGS+=-DYP
DPADD=	${LIBRPCSVC} ${LIBMP} ${LIBGMP}
LDADD=	-lrpcsvc -lmp -lgmp

.include <bsd.prog.mk>
