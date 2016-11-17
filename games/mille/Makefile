#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/mille/Makefile,v 1.2.14.1 2001/04/25 09:28:58 ru Exp $

PROG=	mille
MAN=	mille.6
SRCS=	comp.c end.c extern.c init.c mille.c misc.c move.c print.c \
	roll.c save.c types.c varpush.c
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
HIDEGAME=	hidegame

.include <bsd.prog.mk>
