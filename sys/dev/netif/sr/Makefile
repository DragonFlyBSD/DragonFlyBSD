# $FreeBSD: src/sys/modules/sr/Makefile,v 1.2.2.1 2002/06/17 15:11:05 jhay Exp $
# $DragonFly: src/sys/dev/netif/sr/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $
 
.PATH:  ${.CURDIR}/../../dev/sr
KMOD    = if_sr
SRCS    = if_sr.c if_sr_isa.c if_sr_pci.c
SRCS    += device_if.h bus_if.h pci_if.h isa_if.h opt_netgraph.h
  
.include <bsd.kmod.mk>
