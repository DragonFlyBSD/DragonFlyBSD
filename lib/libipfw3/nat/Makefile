LIB=	ipfw3nat
SRCS=	ipfw3_nat.c

INCS=	ipfw3_nat.h

.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

SHLIB_MAJOR=3

.include <bsd.lib.mk>
