# $FreeBSD: src/usr.bin/colldef/Makefile,v 1.16.2.1 2002/08/17 18:57:10 ache Exp $
# $DragonFly: src/usr.bin/colldef/Makefile,v 1.2 2003/06/17 04:29:25 dillon Exp $

PROG=	colldef
SRCS=	parse.y scan.l y.tab.h
LFLAGS=	-8 -i
CFLAGS+=-I. -I${.CURDIR} -I${.CURDIR}/../../lib/libc/locale
CFLAGS+=-DCOLLATE_DEBUG -DYY_NO_UNPUT
LDADD=	-ll
DPADD=	${LIBL}

.include <bsd.prog.mk>
