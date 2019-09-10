#	@(#)Makefile	8.1 (Berkeley) 6/6/93

.PATH: ${.CURDIR}/../who

PROG=	write
SRCS=	write.c utmpentry.c
BINMODE=2555
BINGRP=	tty

DPADD+=	${LIBUTIL}
LDADD+=	-lutil

CFLAGS+= -I${.CURDIR}/../who

.include <bsd.prog.mk>
