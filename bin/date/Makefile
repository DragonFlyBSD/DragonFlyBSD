#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/bin/date/Makefile,v 1.8 1999/08/27 23:13:58 peter Exp $
# $DragonFly: src/bin/date/Makefile,v 1.2 2003/06/17 04:22:49 dillon Exp $

PROG=	date
SRCS=	date.c netdate.c vary.c
DPADD=	${LIBUTIL}
LDADD=	-lutil

.include <bsd.prog.mk>
