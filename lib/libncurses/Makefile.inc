# $DragonFly: src/lib/libncurses/Makefile.inc,v 1.2 2005/03/12 20:57:07 joerg Exp $

NCURSESDIR=	${.CURDIR}/../../../contrib/ncurses-5.4/

CFLAGS+=	-I${NCURSESDIR}/include/
CFLAGS+=	-I${.CURDIR}/../include/
CFLAGS+=	-I${.CURDIR} -I${.OBJDIR}
