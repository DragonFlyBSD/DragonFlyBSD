# $DragonFly: src/sys/netproto/802_11/wlan_wep/Makefile,v 1.1 2006/05/18 13:51:46 sephe Exp $

KMOD	 = wlan_wep
SRCS	 = ieee80211_crypto_wep.c
SRCS	+= opt_wlan.h

.if !defined(BUILDING_WITH_KERNEL)
opt_wlan.h:
	echo "#define IEEE80211_SUPPORT_TDMA    1" > ${.TARGET}
	echo "#define IEEE80211_SUPPORT_MESH    1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
