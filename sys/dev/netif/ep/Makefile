# $DragonFly: src/sys/dev/netif/ep/Makefile,v 1.1 2004/03/15 17:19:19 dillon Exp $
#

.PATH:	${.CURDIR}/../../dev/ep
KMOD	= if_ep
SRCS	= if_ep.c if_ep_pccard.c if_ep_isa.c
SRCS	+= device_if.h bus_if.h pci_if.h isa_if.h card_if.h
# KMODDEPS = miibus

.include <bsd.kmod.mk>
