# $DragonFly: src/bin/cpdup/Makefile,v 1.1 2003/12/01 02:20:14 dillon Exp $
#

PROG=	cpdup
SRCS=	cpdup.c misc.c
LDADD+= -lmd
DPADD+= ${LIBMD}
CFLAGS+= -Wall -Wstrict-prototypes

.include <bsd.prog.mk>

