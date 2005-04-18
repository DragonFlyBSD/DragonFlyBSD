#	@(#)Makefile	8.1 (Berkeley) 6/7/93
# $FreeBSD: src/usr.bin/mklocale/Makefile,v 1.21.2.2 2001/04/25 11:29:28 ru Exp $
# $DragonFly: src/usr.bin/mklocale/Makefile,v 1.3 2005/04/18 07:43:45 joerg Exp $

PROG=	mklocale
SRCS=	yacc.y lex.l y.tab.h
CFLAGS+= -I. -I${.CURDIR}

SRCS+=		runetype.h ctype.h
CLEANFILES+=	runetype.h ctype.h

runetype.h: ${.CURDIR}/../../include/runetype.h
	cp ${.ALLSRC} ${.TARGET}
ctype.h: ${.CURDIR}/../../include/ctype.h
	grep '#define._CTYPE_' ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>
