# $DragonFly: src/lib/libncurses/Makefile.inc,v 1.4 2005/04/21 13:42:34 joerg Exp $

NCURSESDIR=	${.CURDIR}/../../../contrib/ncurses-5.4/

CFLAGS+=	-I${NCURSESDIR}/include/
CFLAGS+=	-I${.CURDIR}/../include/
CFLAGS+=	-I${.CURDIR} -I${.OBJDIR}

SHLIB_MAJOR=	6
