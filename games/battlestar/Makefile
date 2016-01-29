#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/battlestar/Makefile,v 1.5.2.3 2002/08/07 16:31:41 ru Exp $

PROG=	battlestar
SRCS=	battlestar.c command1.c command2.c command3.c command4.c \
	command5.c command6.c command7.c \
	init.c cypher.c getcom.c parse.c room.c save.c fly.c misc.c \
	globals.c dayfile.c nightfile.c dayobjs.c nightobjs.c words.c
MAN=	battlestar.6
DPADD=	${LIBNCURSES} ${LIBTERMCAP}
LDADD=	-lprivate_ncurses -lprivate_termcap
HIDEGAME=hidegame
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv
beforeinstall:
.if !exists(${DESTDIR}/var/games/battlestar.log)
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 664 /dev/null \
	    ${DESTDIR}/var/games/battlestar.log
.endif

.include <bsd.prog.mk>
