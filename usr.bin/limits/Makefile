#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $DragonFly: src/usr.bin/limits/Makefile,v 1.2 2005/01/12 01:20:26 cpressey Exp $

PROG=	limits
WARNS?=	6
SRCS=	limits.c

CFLAGS+=-Wall
LDADD+= -lutil
DPADD+= ${LIBUTIL}

BINMODE=0555

.include <bsd.prog.mk>
