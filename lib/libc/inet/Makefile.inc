.PATH:	${.CURDIR}/inet

CMAPS+=	${.CURDIR}/inet/Symbol.map

SRCS+=	inet_addr.c inet_cidr_ntop.c inet_cidr_pton.c inet_lnaof.c \
	inet_makeaddr.c inet_net_ntop.c inet_net_pton.c inet_neta.c \
	inet_netof.c inet_network.c inet_ntoa.c inet_ntop.c \
	inet_pton.c nsap_addr.c

.if ${LIB} == "c"
MAN+=	inet.3 inet_net.3

MLINKS+=inet.3 addr.3 \
	inet.3 inet_addr.3 \
	inet.3 inet_aton.3 \
	inet.3 inet_lnaof.3 \
	inet.3 inet_makeaddr.3 \
	inet.3 inet_netof.3 \
	inet.3 inet_network.3 \
	inet.3 inet_ntoa.3 \
	inet.3 inet_ntoa_r.3 \
	inet.3 inet_ntop.3 \
	inet.3 inet_pton.3 \
	inet.3 network.3 \
	inet.3 ntoa.3 
MLINKS+=inet_net.3 inet_net_ntop.3 \
	inet_net.3 inet_net_pton.3
.endif
