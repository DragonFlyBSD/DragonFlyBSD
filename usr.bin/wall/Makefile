#	@(#)Makefile	8.1 (Berkeley) 6/6/93

.PATH: ${.CURDIR}/../who

PROG=	wall
SRCS=	ttymsg.c wall.c utmpentry.c
BINGRP=	tty
BINMODE=2555

DPADD+=	${LIBUTIL}
LDADD+=	-lutil

CFLAGS+= -I${.CURDIR}/../who

.include <bsd.prog.mk>
