# $FreeBSD: src/usr.sbin/pkg_install/info/Makefile,v 1.11.2.4 2003/01/09 12:05:53 des Exp $
# $DragonFly: src/usr.sbin/pkg_install/info/Attic/Makefile,v 1.2 2003/06/17 04:29:59 dillon Exp $

PROG=	pkg_info
SRCS=	main.c perform.c show.c

CFLAGS+= ${DEBUG} -I${.CURDIR}/../lib

WARNS?=	2

DPADD=	${LIBINSTALL} ${LIBFETCH} ${LIBMD}
LDADD=	${LIBINSTALL} -lfetch -lmd

.if !defined(NOCRYPT) && !defined(NOSECURE) && !defined(NO_OPENSSL)
DPADD+=	${LIBSSL} ${LIBCRYPTO}
LDADD+=	-lssl -lcrypto
.endif

.include <bsd.prog.mk>
