#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/backgammon/backgammon/Makefile,v 1.2.14.1 2001/04/25 09:28:44 ru Exp $

PROG=	backgammon
CFLAGS+=-I${.CURDIR}/../common_source
SRCS=	allow.c board.c check.c extra.c fancy.c init.c main.c move.c \
	odds.c one.c save.c subs.c table.c text.c version.c
MAN=	backgammon.6
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
HIDEGAME=	hidegame

.PATH:	${.CURDIR}/../common_source

# XXX: mismatches between types for static init
init.o:	init.c
	${CC} ${CFLAGS:N-flto} -c -o ${.TARGET} ${.IMPSRC}

.include "../../Makefile.inc"
.include <bsd.prog.mk>
