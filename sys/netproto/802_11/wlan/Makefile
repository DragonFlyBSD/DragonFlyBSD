# $DragonFly: src/sys/netproto/802_11/wlan/Makefile,v 1.4 2005/02/18 11:41:42 corecode Exp $

KMOD	= wlan
SRCS	= ieee80211.c ieee80211_crypto.c ieee80211_input.c ieee80211_ioctl.c
SRCS	+= ieee80211_node.c ieee80211_output.c ieee80211_proto.c
SRCS	+= bus_if.h device_if.h opt_inet.h opt_ipx.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET	1" > ${.TARGET}

opt_ipx.h:
	echo "#define IPX	1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
