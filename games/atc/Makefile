#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/atc/Makefile,v 1.14.2.3 2002/02/18 16:02:41 ru Exp $

PROG=	atc
CFLAGS+=-I${.CURDIR} -I.
SRCS=	extern.c grammar.y graphics.c input.c lex.l list.c log.c \
	main.c tunable.c update.c y.tab.h
MAN=	atc.6
VARGAMES=
GAMESCURSES=

DPADD=	${LIBL} ${LIBM}
LDADD=	-ll -lm

.if !defined(NO_SHARE)
FILES=		Game_List Killer crossover default easy game_2
FILESDIR=	${SHAREDIR}/games/atc
.endif

beforeinstall:
.if !exists(${DESTDIR}/var/games/atc_score)
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 664 /dev/null \
	    ${DESTDIR}/var/games/atc_score
.endif

.PATH: ${.CURDIR}/games

.include <bsd.prog.mk>
