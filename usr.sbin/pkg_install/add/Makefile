# $FreeBSD: src/usr.sbin/pkg_install/add/Makefile,v 1.21 2004/08/12 20:06:00 ru Exp $
# $DragonFly: src/usr.sbin/pkg_install/add/Attic/Makefile,v 1.5 2005/03/08 19:11:30 joerg Exp $

PROG=	pkg_add
SRCS=	main.c perform.c futil.c extract.c

CFLAGS+= -I${.CURDIR}/../lib

WARNS?=	3

DPADD=	${LIBINSTALL} ${LIBFETCH} ${LIBMD}
LDADD=	${LIBINSTALL} -lfetch -lmd

.include <bsd.prog.mk>
