#	$OpenBSD: Makefile,v 1.7 2014/07/16 20:00:14 okan Exp $

PROG=	hangman
SRCS=	endgame.c extern.c getguess.c getsym.c getword.c main.c playgame.c \
	prdata.c prman.c prword.c setup.c
MAN=	hangman.6
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
