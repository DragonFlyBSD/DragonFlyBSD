LIB=    ipfw3dummynet
SRCS=	ipfw3_dummynet.c

INCS=	ipfw3_dummynet.h

.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

SHLIB_MAJOR=3

.include <bsd.lib.mk>
