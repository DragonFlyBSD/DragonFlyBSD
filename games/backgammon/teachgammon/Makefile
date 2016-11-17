#	@(#)Makefile	8.1 (Berkeley) 5/31/93

PROG=	teachgammon
NOMAN=	noman
CFLAGS+=-DTEACHGAMMON_TEXT -I${.CURDIR}/../common_source
SRCS=	allow.c board.c check.c data.c fancy.c init.c odds.c one.c save.c \
	subs.c table.c teach.c ttext1.c ttext2.c tutor.c
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
