# $DragonFly: src/sys/dev/netif/ath/Makefile.inc,v 1.2 2007/02/22 05:17:09 sephe Exp $

HAL	= ${.CURDIR}/../ath_hal
HALOSDEP= ${.CURDIR}/../ath_hal
CFLAGS	+= -I${HALOSDEP}

opt_inet.h:
	echo "#define INET	1" > ${.TARGET}
	echo "#define INET6     1" >> ${.TARGET}

opt_wlan.h:
	echo "#define IEEE80211_DEBUG           1" > ${.TARGET}
	echo "#define IEEE80211_SUPPORT_TDMA    1" >> ${.TARGET}
	echo "#define IEEE80211_SUPPORT_MESH    1" >> ${.TARGET}

opt_ah.h:
	echo "#define AH_SUPPORT_AR5416 	1" > ${.TARGET}

#opt_ktr.h:
#	echo "" > ${.TARGET}
