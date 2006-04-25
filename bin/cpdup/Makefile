# $DragonFly: src/bin/cpdup/Makefile,v 1.5 2006/04/25 21:30:45 dillon Exp $
#

PROG=	cpdup
SRCS=	cpdup.c misc.c md5.c fsmid.c

LDADD+= -lmd
DPADD+= ${LIBMD}

.include <bsd.prog.mk>

