# $FreeBSD: src/usr.sbin/pkg_install/delete/Makefile,v 1.21 2004/06/29 19:06:41 eik Exp $
# $DragonFly: src/usr.sbin/pkg_install/delete/Attic/Makefile,v 1.4 2004/07/30 04:46:13 dillon Exp $

PROG=	pkg_delete
SRCS=	main.c perform.c

CFLAGS+= ${DEBUG} -I${.CURDIR}/../lib

WARNS?=	6
WFORMAT?=	1

DPADD=	${LIBINSTALL} ${LIBMD}
LDADD=	${LIBINSTALL} -lmd

.include <bsd.prog.mk>
