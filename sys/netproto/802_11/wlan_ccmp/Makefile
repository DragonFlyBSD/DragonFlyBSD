# $DragonFly: src/sys/netproto/802_11/wlan_ccmp/Makefile,v 1.1 2006/05/18 13:51:46 sephe Exp $

.PATH: ${.CURDIR}/../../../../crypto/rijndael

KMOD	= wlan_ccmp
SRCS	= ieee80211_crypto_ccmp.c
KMODDEPS= wlan

.include <bsd.kmod.mk>
