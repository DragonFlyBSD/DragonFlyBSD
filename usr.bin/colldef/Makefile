# $FreeBSD: src/usr.bin/colldef/Makefile,v 1.16.2.1 2002/08/17 18:57:10 ache Exp $
# $DragonFly: src/usr.bin/colldef/Makefile,v 1.3 2005/03/02 07:16:23 joerg Exp $

PROG=	colldef
SRCS=	parse.y scan.l y.tab.h
LFLAGS=	-8 -i
CFLAGS+=-I. -I${.CURDIR}
CFLAGS+=-DCOLLATE_DEBUG -DYY_NO_UNPUT
LDADD=	-ll
DPADD=	${LIBL}
WARNS?=	6

SRCS+=		collate.h
CLEANFILES+=	collate.h

collate.h: ${.CURDIR}/../../lib/libc/locale/collate.h
	cp ${.CURDIR}/../../lib/libc/locale/collate.h ${.TARGET}

.include <bsd.prog.mk>
