# $FreeBSD: src/usr.sbin/pkg_install/create/Makefile,v 1.21 2004/07/28 07:19:15 kan Exp $
# $DragonFly: src/usr.sbin/pkg_install/create/Attic/Makefile,v 1.4 2004/07/30 04:46:12 dillon Exp $

PROG=	pkg_create
SRCS=	main.c perform.c pl.c

CFLAGS+= ${DEBUG} -I${.CURDIR}/../lib

WARNS?=	3
WFORMAT?=	1

DPADD=	${LIBINSTALL} ${LIBMD}
LDADD=	${LIBINSTALL} -lmd

.include <bsd.prog.mk>
