# $FreeBSD: src/usr.sbin/pkg_install/info/Makefile,v 1.19 2004/06/29 19:06:42 eik Exp $
# $DragonFly: src/usr.sbin/pkg_install/info/Attic/Makefile,v 1.4 2004/07/30 04:46:13 dillon Exp $

PROG=	pkg_info
SRCS=	main.c perform.c show.c

CFLAGS+= ${DEBUG} -I${.CURDIR}/../lib

WARNS?=	6
WFORMAT?=	1

DPADD=	${LIBINSTALL} ${LIBFETCH} ${LIBMD}
LDADD=	${LIBINSTALL} -lfetch -lmd

.include <bsd.prog.mk>
