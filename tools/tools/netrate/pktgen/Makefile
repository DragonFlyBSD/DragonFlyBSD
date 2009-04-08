# $DragonFly: src/tools/tools/netrate/pktgen/Makefile,v 1.1 2008/03/26 13:53:14 sephe Exp $

KMOD=	pktgen
SRCS=	pktgen.c

CFLAGS+= -g
CFLAGS+= -DINVARIANTS 

# For SMP kernel
.ifdef SMP
CFLAGS+= -DSMP
.endif

.include <bsd.kmod.mk>
