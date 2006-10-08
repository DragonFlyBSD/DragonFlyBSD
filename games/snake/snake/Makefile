#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/snake/snake/Makefile,v 1.5.2.3 2002/08/07 16:31:42 ru Exp $
# $DragonFly: src/games/snake/snake/Makefile,v 1.5 2006/10/08 16:22:36 pavalos Exp $

PROG=	snake
SRCS=	snake.c
MAN=	snake.6
DPADD=	${LIBM} ${LIBCURSES}
LDADD=	-lm -lcurses
HIDEGAME=hidegame
MLINKS=	snake.6 snscore.6

beforeinstall:
.if !exists(${DESTDIR}/var/games/snakerawscores)
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 664 /dev/null \
	    ${DESTDIR}/var/games/snakerawscores
.endif
.if !exists(${DESTDIR}/var/games/snake.log)
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 664 /dev/null \
	    ${DESTDIR}/var/games/snake.log
.endif

.include "../../Makefile.inc"
.include <bsd.prog.mk>
