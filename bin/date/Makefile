#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/bin/date/Makefile,v 1.8 1999/08/27 23:13:58 peter Exp $
# $DragonFly: src/bin/date/Makefile,v 1.5 2005/02/06 06:16:39 okumoto Exp $

PROG=	date
SRCS=	date.c netdate.c vary.c

DPADD=	${LIBUTIL}
LDADD=	-lutil

.include <bsd.prog.mk>
