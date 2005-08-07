# $DragonFly: src/lib/libncurses/Makefile.inc,v 1.5 2005/08/07 08:40:42 asmodai Exp $

NCURSESDIR=	${.CURDIR}/../../../contrib/ncurses-5.4

CFLAGS+=	-I${NCURSESDIR}/include/
CFLAGS+=	-I${.CURDIR}/../include/
CFLAGS+=	-I${.CURDIR} -I${.OBJDIR}

SHLIB_MAJOR=	6
