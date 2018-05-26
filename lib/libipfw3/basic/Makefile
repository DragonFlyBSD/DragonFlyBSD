LIB=	ipfw3basic
SRCS=	ipfw3_basic.c

INCS=	ipfw3_basic.h

.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

SHLIB_MAJOR=3

.include <bsd.lib.mk>
