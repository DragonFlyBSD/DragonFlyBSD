# $DragonFly: src/bin/cpdup/Makefile,v 1.2 2004/09/26 15:32:56 asmodai Exp $
#

PROG=	cpdup
SRCS=	cpdup.c misc.c

WARNS?=	6

LDADD+= -lmd
DPADD+= ${LIBMD}
CFLAGS+= -Wall -Wstrict-prototypes

.include <bsd.prog.mk>

