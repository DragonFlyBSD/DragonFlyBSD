# $DragonFly: src/sys/dev/netif/ral/Makefile,v 1.3 2007/04/02 14:25:38 sephe Exp $
KMOD	= if_ral

SRCS	= if_ral_pci.c rt2560.c rt2661.c
SRCS	+= device_if.h bus_if.h pci_if.h

KMODDEPS= wlan wlan_ratectl_onoe wlan_ratectl_sample

.include <bsd.kmod.mk>
