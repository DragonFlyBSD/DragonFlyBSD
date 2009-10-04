#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $DragonFly: src/usr.bin/wall/Makefile,v 1.2 2007/08/27 16:51:01 pavalos Exp $

.PATH: ${.CURDIR}/../../usr.bin/who

PROG=	wall
SRCS=	ttymsg.c wall.c utmpentry.c
BINGRP=	tty
BINMODE=2555

DPADD+=	${LIBUTIL}
LDADD+=	-lutil

CFLAGS+= -I${.CURDIR}/../../usr.bin/who -DSUPPORT_UTMP -DSUPPORT_UTMPX

.include <bsd.prog.mk>
