# $DragonFly: src/bin/cpdup/Makefile,v 1.3 2004/11/14 18:18:30 joerg Exp $
#

PROG=	cpdup
SRCS=	cpdup.c misc.c

WARNS?=	6

LDADD+= -lmd
DPADD+= ${LIBMD}

.include <bsd.prog.mk>

