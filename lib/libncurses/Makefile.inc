NCURSESDIR=	${.CURDIR}/../../../contrib/ncurses

SHLIB_MAJOR=	60
VERSION_DEF=	${.CURDIR}/../Versions.def
SYMBOL_MAPS=	${.CURDIR}/../Symbol.base.map
TERMINFO_CAPS=  ${NCURSESDIR}/include/Caps

.if defined (ENABLE_WIDEC)
CFLAGS+=	-D_XOPEN_SOURCE_EXTENDED
CFLAGS+=	-DENABLE_WIDEC
SW_WIDEC=	1
SW_TYPE=	cchar_t
LIB_SUFFIX=	w
SYMBOL_MAPS+=	${.CURDIR}/../Symbol.wide.map
.else
SW_WIDEC=	0
SW_TYPE=	chtype
LIB_SUFFIX=
.endif

CFLAGS+=	-I.
CFLAGS+=	-I${NCURSESDIR}/include
CFLAGS+=	-I${NCURSESDIR}/ncurses
CFLAGS+=	-I${.CURDIR}/../include
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}

WARNS?=	5

.PATH:	${NCURSESDIR}/include ${.CURDIR}/../include
.if defined(ZPANEL)
.PATH:	${NCURSESDIR}/panel
.endif
.if defined(ZNCURSES)
.PATH:	${NCURSESBASEDIR}
.PATH:	${NCURSESSERIALDIR}
.PATH:	${NCURSESTINFODIR}
.PATH:	${NCURSESDIR}/ncurses/trace
. if defined (ENABLE_WIDEC)
.PATH:	${NCURSESDIR}/ncurses/widechar
. endif
.endif

ncurses_def.h:	MKncurses_def.sh ncurses_defs
	AWK=awk sh ${NCURSESDIR}/include/MKncurses_def.sh \
	${NCURSESDIR}/include/ncurses_defs > ${.TARGET}

curses.h: $(TERMINFO_CAPS) curses.head curses.tail MKkey_defs.sh
	cat ${.CURDIR}/../include/curses.head | \
	sed -e 's|SET_NEED_WCHAR_H|${SW_WIDEC}|' \
	    -e 's|SET_NCURSES_CH_T|${SW_TYPE}|' > ${.TARGET}.new
	AWK=awk sh ${NCURSESDIR}/include/MKkey_defs.sh $(TERMINFO_CAPS) >> ${.TARGET}.new
.if defined(ENABLE_WIDEC)
	cat ${NCURSESDIR}/include/curses.wide >> ${.TARGET}.new
.endif
	cat ${NCURSESDIR}/include/curses.tail >> ${.TARGET}.new
	mv -f ${.TARGET}.new ${.TARGET}

.include "../Makefile.inc"
