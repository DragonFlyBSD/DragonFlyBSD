# $DragonFly: src/bin/cpdup/Makefile,v 1.6 2006/08/13 20:51:40 dillon Exp $
#

PROG=	cpdup
SRCS=	cpdup.c hcproto.c hclink.c misc.c fsmid.c

.if !defined(NOMD5)
SRCS+=	md5.c
.endif

LDADD+= -lmd
DPADD+= ${LIBMD}

.include <bsd.prog.mk>

