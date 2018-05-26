LIB=	ipfw3layer4
SRCS=	ipfw3_layer4.c

INCS=	ipfw3_layer4.h

.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

LDADD=-lpcap
DPADD=${LIBPCAP}
SHLIB_MAJOR=3

.include <bsd.lib.mk>
