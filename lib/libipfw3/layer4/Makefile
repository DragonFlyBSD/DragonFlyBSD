LIB=	ipfw3layer4
SRCS=	ipfw3_layer4.c

INCS=	ipfw3_layer4.h

.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif
CFLAGS+=-DPCAP_DONT_INCLUDE_PCAP_BPF_H

LDADD=-lpcap
DPADD=${LIBPCAP}
SHLIB_MAJOR=3

.include <bsd.lib.mk>
