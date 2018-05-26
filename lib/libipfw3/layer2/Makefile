LIB=    ipfw3layer2
SRCS=	ipfw3_layer2.c

INCS=	ipfw3_layer2.h

.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

SHLIB_MAJOR=3

.include <bsd.lib.mk>
