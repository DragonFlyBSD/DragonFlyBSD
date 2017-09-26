KMOD=	pktgen
SRCS=	pktgen.c

KCFLAGS+= -g
.if !defined(NOINVAR)
KCFLAGS+= -DINVARIANTS
.endif

.include <bsd.kmod.mk>
