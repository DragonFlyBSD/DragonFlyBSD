# $DragonFly: src/sys/netproto/802_11/Makefile,v 1.1 2004/07/26 16:30:17 joerg Exp $

KMOD	= wlan
SRCS	= ieee80211.c ieee80211_crypto.c ieee80211_input.c ieee80211_ioctl.c
SRCS	+= ieee80211_node.c ieee80211_output.c ieee80211_proto.c
SRCS	+= bus_if.h device_if.h opt_inet.h opt_ipx.h

.include <bsd.kmod.mk>
