# $FreeBSD: src/usr.bin/gencat/Makefile,v 1.5 1999/08/28 01:01:40 peter Exp $
# $DragonFly: src/usr.bin/gencat/Makefile,v 1.4 2007/08/27 16:50:54 pavalos Exp $

PROG=	gencat
SRCS=	gencat.c
WARNS?=	2

.if defined(BOOTSTRAPPING)
nl_types.h:	../../include/nl_types.h
	cp ${.CURDIR}/../../include/nl_types.h ${.TARGET}

types.h: ${.CURDIR}/../../sys/sys/types.h
	mkdir sys
	cp ${.ALLSRC} sys/${.TARGET}

SRCS+=		nl_types.h types.h
CFLAGS+=	-I${.OBJDIR}
CLEANFILES+=	nl_types.h
CLEANDIRS+=	sys
.endif

.include <bsd.prog.mk>
