#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/bin/date/Makefile,v 1.8 1999/08/27 23:13:58 peter Exp $
# $DragonFly: src/bin/date/Makefile,v 1.3 2004/09/26 15:34:16 asmodai Exp $

PROG=	date
SRCS=	date.c netdate.c vary.c

WARNS?=	3

DPADD=	${LIBUTIL}
LDADD=	-lutil

.include <bsd.prog.mk>
