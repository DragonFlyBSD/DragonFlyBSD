# $FreeBSD: src/usr.sbin/pkg_install/create/Makefile,v 1.11.2.3 2003/01/09 12:05:53 des Exp $
# $DragonFly: src/usr.sbin/pkg_install/create/Attic/Makefile,v 1.2 2003/06/17 04:29:59 dillon Exp $

PROG= pkg_create

CFLAGS+=	${DEBUG} -I${.CURDIR}/../lib

WARNS?=	2

DPADD=	${LIBINSTALL} ${LIBFETCH} ${LIBMD}
LDADD=	${LIBINSTALL} -lfetch -lmd

.if !defined(NOCRYPT) && !defined(NOSECURE) && !defined(NO_OPENSSL)
DPADD+=	${LIBSSL} ${LIBCRYPTO}
LDADD+=	-lssl -lcrypto
.endif

SRCS= main.c perform.c pl.c

.include <bsd.prog.mk>
