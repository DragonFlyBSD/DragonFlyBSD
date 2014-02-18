# $FreeBSD: src/usr.bin/colldef/Makefile,v 1.16.2.1 2002/08/17 18:57:10 ache Exp $

PROG=	colldef
SRCS=	parse.y scan.l y.tab.h
LFLAGS=	-8 -i
CFLAGS+=-I. -I${.CURDIR}
CFLAGS+=-I${.CURDIR}/../../include
CFLAGS+=-I${.CURDIR}/../../lib/libc/locale
CFLAGS+=-DCOLLATE_DEBUG
LDADD=	-ll
DPADD=	${LIBL}

.if defined(BOOTSTRAPPING)
SRCS+=		sys/types.h ctype.h
CLEANFILES+=	ctype.h
CLEANDIRS+=	sys
CFLAGS+=	-D__heedresult=
.endif

sys/types.h: /usr/include/sys/types.h
	mkdir -p sys
	cp ${.ALLSRC} ${.TARGET}

ctype.h: /usr/include/ctype.h
	cp ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
