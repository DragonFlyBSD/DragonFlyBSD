# $DragonFly: src/lib/libncurses/Makefile.inc,v 1.3 2005/03/14 15:15:13 joerg Exp $

NCURSESDIR=	${.CURDIR}/../../../contrib/ncurses-5.4/

CFLAGS+=	-I${NCURSESDIR}/include/
CFLAGS+=	-I${.CURDIR}/../include/
CFLAGS+=	-I${.CURDIR} -I${.OBJDIR}

SHLIB_MAJOR=	5
SHLIB_MINOR=	4
