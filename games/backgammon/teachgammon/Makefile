#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $DragonFly: src/games/backgammon/teachgammon/Makefile,v 1.3 2006/10/08 16:22:35 pavalos Exp $

PROG=	teachgammon
CFLAGS+=-DTEACHGAMMON_TEXT -I${.CURDIR}/../common_source
SRCS=	allow.c board.c check.c data.c fancy.c init.c odds.c one.c save.c \
	subs.c table.c teach.c ttext1.c ttext2.c tutor.c
DPADD=	${LIBTERMCAP}
LDADD=	-ltermcap
HIDEGAME=hidegame
NOMAN=	noman

.PATH:	${.CURDIR}/../common_source

.include "../../Makefile.inc"
.include <bsd.prog.mk>
