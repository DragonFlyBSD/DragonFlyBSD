# $DragonFly: src/lib/libncurses/libpanel/Makefile,v 1.5 2005/12/06 02:22:23 corecode Exp $

LIB=    panel

.include "../Makefile.inc"

NCURSESPANELDIR=${NCURSESDIR}/panel ${.OBJDIR}

.PATH: ${NCURSESPANELDIR}

CFLAGS+= 	-I${NCURSESDIR}/menu -I${NCURSESDIR}/ncurses

TERMINFO_CAPS=  ${NCURSESDIR}/include/Caps

ncurses_def.h:
	sh ${NCURSESDIR}/include/MKncurses_def.sh ${NCURSESDIR}/include/ncurses_defs > ${.TARGET}
curses.h:
	cat ${.CURDIR}/../include/curses.head >$@
	sh ${NCURSESDIR}/include/MKkey_defs.sh $(TERMINFO_CAPS) >>$@
	cat ${NCURSESDIR}/include/curses.wide >>$@
	cat ${NCURSESDIR}/include/curses.tail >>$@

CLEANFILES=	curses.h ncurses_def.h
SRCS=	\
	ncurses_def.h \
	curses.h

SRCS+=	\
	panel.c \
	p_above.c \
	p_below.c \
	p_bottom.c \
	p_delete.c \
	p_hide.c \
	p_hidden.c \
	p_move.c \
	p_new.c \
	p_replace.c \
	p_show.c \
	p_top.c \
	p_update.c \
	p_user.c \
	p_win.c

INCS=	panel.h

.include <bsd.lib.mk>

