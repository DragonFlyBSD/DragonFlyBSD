# $DragonFly: src/sys/netproto/802_11/wlan/Makefile,v 1.2 2004/07/27 12:33:01 hmp Exp $

KMOD	= wlan
SRCS	= ieee80211.c ieee80211_crypto.c ieee80211_input.c ieee80211_ioctl.c
SRCS	+= ieee80211_node.c ieee80211_output.c ieee80211_proto.c
SRCS	+= bus_if.h device_if.h opt_inet.h opt_ipx.h

# INET
.if defined(INET)
opt_inet.h:
	echo "#define INET	1" > ${.TARGET}
.endif

# IPX
.if defined(IPX)
opt_ipx.h:
	echo "#define IPX	1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
