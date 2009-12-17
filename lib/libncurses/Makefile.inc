# $DragonFly: src/lib/libncurses/Makefile.inc,v 1.6 2005/09/22 11:14:44 asmodai Exp $

NCURSESDIR=	${.CURDIR}/../../../contrib/ncurses

CFLAGS+=	-I${NCURSESDIR}/include/
CFLAGS+=	-I${.CURDIR}/../include/
CFLAGS+=	-I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-D_XOPEN_SOURCE_EXTENDED

SHLIB_MAJOR=	6
