# $DragonFly: src/bin/cpdup/Makefile,v 1.6.6.1 2008/04/16 17:45:18 dillon Exp $
#

PROG=	cpdup
SRCS=	cpdup.c hcproto.c hclink.c misc.c fsmid.c

.if !defined(NOPTHREADS)
CFLAGS += -DUSE_PTHREADS=1 -pthread
.endif

.if !defined(NOMD5)
SRCS+=	md5.c
.endif

LDADD+= -lmd
DPADD+= ${LIBMD}

.include <bsd.prog.mk>

