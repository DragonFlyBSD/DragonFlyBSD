# $FreeBSD: src/usr.bin/gencat/Makefile,v 1.5 1999/08/28 01:01:40 peter Exp $
# $DragonFly: src/usr.bin/gencat/Makefile,v 1.3 2005/04/21 16:36:35 joerg Exp $

PROG=	gencat
SRCS=	gencat.c

.if defined(BOOTSTRAPPING)
nl_types.h:	../../include/nl_types.h
	cp ${.CURDIR}/../../include/nl_types.h ${.TARGET}
SRCS+=	nl_types.h
CFLAGS+=	-I${.OBJDIR}
CLEANFILES+=	nl_types.h
.endif

.include <bsd.prog.mk>
