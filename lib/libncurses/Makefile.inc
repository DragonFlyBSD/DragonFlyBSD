NCURSESDIR=	${.CURDIR}/../../../contrib/ncurses

SHLIB_MAJOR=	60
VERSION_DEF=	${.CURDIR}/../Versions.def
SYMBOL_MAPS=	${.CURDIR}/../Symbol.base.map
TERMINFO_CAPS=  ${NCURSESDIR}/include/Caps

.if defined(ENABLE_WIDEC)
CFLAGS+=	-D_XOPEN_SOURCE_EXTENDED
CFLAGS+=	-DENABLE_WIDEC
SW_WIDEC=	1
SW_TYPE=	cchar_t
SYMBOL_MAPS+=	${.CURDIR}/../Symbol.wide.map
.else
SW_WIDEC=	0
SW_TYPE=	chtype
.endif

CFLAGS+=	-I.
CFLAGS+=	-I${NCURSESDIR}/include
CFLAGS+=	-I${NCURSESDIR}/ncurses
CFLAGS+=	-I${.CURDIR}/../include
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}

WARNS?=	5

NCURSESBASEDIR=		${NCURSESDIR}/ncurses/base
NCURSESSERIALDIR=	${NCURSESDIR}/ncurses/tty
NCURSESTINFODIR=	${NCURSESDIR}/ncurses/tinfo
NCURSESTRACEDIR=	${NCURSESDIR}/ncurses/trace
NCURSESWIDEDIR=		${NCURSESDIR}/ncurses/widechar

.PATH:	${NCURSESDIR}/include ${.CURDIR}/../include
.if defined(ZPANEL)
.PATH:	${NCURSESDIR}/panel
.endif
.if defined(ZNCURSES) || defined(ZTIC)
.PATH:	${NCURSESBASEDIR}
.PATH:	${NCURSESSERIALDIR}
.PATH:	${NCURSESTINFODIR}
.PATH:	${NCURSESTRACEDIR}
. if defined (ENABLE_WIDEC)
.PATH:	${NCURSESWIDEDIR}
. endif
.endif

.include "../Makefile.inc"
