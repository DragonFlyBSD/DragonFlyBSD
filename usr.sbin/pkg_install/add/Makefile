# $FreeBSD: src/usr.sbin/pkg_install/add/Makefile,v 1.20 2004/07/28 07:19:15 kan Exp $
# $DragonFly: src/usr.sbin/pkg_install/add/Attic/Makefile,v 1.4 2004/07/30 04:46:12 dillon Exp $

PROG=	pkg_add
SRCS=	main.c perform.c futil.c extract.c

CFLAGS+= ${DEBUG} -I${.CURDIR}/../lib

WARNS?=	3
WFORMAT?=	1

DPADD=	${LIBINSTALL} ${LIBFETCH} ${LIBMD}
LDADD=	${LIBINSTALL} -lfetch -lmd

.include <bsd.prog.mk>
