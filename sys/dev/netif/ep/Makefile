# $DragonFly: src/sys/dev/netif/ep/Makefile,v 1.2 2005/05/31 08:10:44 joerg Exp $
#

KMOD	= if_ep
SRCS	= if_ep.c if_ep_pccard.c if_ep_isa.c
SRCS	+= device_if.h bus_if.h pci_if.h isa_if.h card_if.h

.include <bsd.kmod.mk>
