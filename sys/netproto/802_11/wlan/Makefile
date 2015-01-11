KMOD	 = wlan
SRCS	 = ieee80211.c ieee80211_action.c ieee80211_adhoc.c ieee80211_ageq.c
#SRCS	+= ieee80211_amrr.c ieee80211_alq.c
SRCS	+= ieee80211_crypto.c ieee80211_crypto_none.c ieee80211_ddb.c
SRCS	+= ieee80211_dfs.c ieee80211_dragonfly.c ieee80211_hostap.c
SRCS	+= ieee80211_ht.c ieee80211_hwmp.c ieee80211_input.c ieee80211_ioctl.c
SRCS	+= ieee80211_mesh.c ieee80211_monitor.c ieee80211_node.c
SRCS	+= ieee80211_output.c ieee80211_phy.c ieee80211_power.c
SRCS	+= ieee80211_proto.c ieee80211_radiotap.c
SRCS	+= ieee80211_ratectl.c ieee80211_ratectl_none.c
SRCS	+= ieee80211_regdomain.c ieee80211_rssadapt.c
SRCS	+= ieee80211_scan.c ieee80211_scan_sta.c ieee80211_sta.c
SRCS	+= ieee80211_superg.c ieee80211_tdma.c ieee80211_wds.c
SRCS	+= bus_if.h device_if.h opt_inet.h opt_inet6.h opt_wlan.h
SRCS	+= opt_tdma.h opt_ddb.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET	1" > ${.TARGET}

opt_inet6.h:
	echo "#define INET6	1" > ${.TARGET}

opt_wlan.h:
	echo "#define IEEE80211_DEBUG		1" > ${.TARGET}
	echo "#define IEEE80211_SUPPORT_TDMA	1" >> ${.TARGET}
	echo "#define IEEE80211_SUPPORT_MESH	1" >> ${.TARGET}

opt_tdma.h:
	echo "" > ${.TARGET}

opt_ddb.h:
	echo "#define DDB	1" > ${.TARGET}

.endif

.include <bsd.kmod.mk>
