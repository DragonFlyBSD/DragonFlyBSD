#	@(#)Makefile	8.1 (Berkeley) 6/7/93
# $FreeBSD: src/usr.bin/mklocale/Makefile,v 1.21.2.2 2001/04/25 11:29:28 ru Exp $

PROG=	mklocale
SRCS=	yacc.y lex.l y.tab.h
CFLAGS+=-I. -I${.CURDIR} -I${.CURDIR}/../../lib/libc/locale
CFLAGS+=-I${.CURDIR}/../../include

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
