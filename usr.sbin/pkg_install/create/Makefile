# $FreeBSD: src/usr.sbin/pkg_install/create/Makefile,v 1.22 2004/08/12 20:06:00 ru Exp $
# $DragonFly: src/usr.sbin/pkg_install/create/Attic/Makefile,v 1.5 2005/03/08 19:11:30 joerg Exp $

PROG=	pkg_create
SRCS=	main.c perform.c pl.c

CFLAGS+= -I${.CURDIR}/../lib

WARNS?=	3

DPADD=	${LIBINSTALL} ${LIBMD}
LDADD=	${LIBINSTALL} -lmd

.include <bsd.prog.mk>
