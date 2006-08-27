#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/mille/Makefile,v 1.2.14.1 2001/04/25 09:28:58 ru Exp $
# $DragonFly: src/games/mille/Makefile,v 1.3 2006/08/27 17:17:23 pavalos Exp $

PROG=	mille
SRCS=	comp.c end.c extern.c init.c mille.c misc.c move.c print.c \
	roll.c save.c types.c varpush.c
DPADD=	${LIBCURSES}
LDADD=	-lcurses
MAN=	mille.6
HIDEGAME=hidegame
WARNS?=	6

.include <bsd.prog.mk>

