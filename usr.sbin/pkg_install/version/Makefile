# $FreeBSD: src/usr.sbin/pkg_install/version/Makefile,v 1.14 2004/06/29 19:06:42 eik Exp $
# $DragonFly: src/usr.sbin/pkg_install/version/Attic/Makefile,v 1.3 2004/07/30 04:46:14 dillon Exp $

PROG=	pkg_version
SRCS=	main.c perform.c

CFLAGS+= ${DEBUG} -I${.CURDIR}/../lib

WARNS?=	6
WFORMAT?=	1

DPADD=	${LIBINSTALL} ${LIBFETCH} ${LIBMD}
LDADD=	${LIBINSTALL} -lfetch -lmd

test:
	./test-pkg_version.sh

.include <bsd.prog.mk>
