# $DragonFly: src/sys/dev/netif/bwi/Makefile,v 1.1 2007/09/08 06:15:54 sephe Exp $
KMOD	= if_bwi

SRCS	= if_bwi.c bwimac.c bwiphy.c bwirf.c
SRCS	+= device_if.h bus_if.h pci_if.h

KMODDEPS= wlan

.include <bsd.kmod.mk>
