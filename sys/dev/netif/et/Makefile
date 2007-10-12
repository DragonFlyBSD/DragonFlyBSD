# $DragonFly: src/sys/dev/netif/et/Makefile,v 1.1 2007/10/12 14:12:42 sephe Exp $

KMOD= if_et
SRCS= if_et.c
SRCS+= miibus_if.h device_if.h bus_if.h pci_if.h
KMODDEPS= miibus

.include <bsd.kmod.mk>
