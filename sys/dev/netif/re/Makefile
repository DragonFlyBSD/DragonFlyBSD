# $DragonFly: src/sys/dev/netif/re/Makefile,v 1.1 2004/07/07 09:47:27 joerg Exp $

KMOD	= if_re
SRCS	= if_re.c device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
