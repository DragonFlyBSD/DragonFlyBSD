# $FreeBSD: src/sys/modules/ar/Makefile,v 1.3.2.1 2002/06/17 15:11:03 jhay Exp $
# $DragonFly: src/sys/dev/netif/ar/Makefile,v 1.2 2003/06/17 04:28:43 dillon Exp $
 
.PATH:  ${.CURDIR}/../../dev/ar
KMOD    = if_ar
SRCS    = if_ar.c if_ar_isa.c if_ar_pci.c
SRCS    += device_if.h bus_if.h pci_if.h isa_if.h opt_netgraph.h
  
.include <bsd.kmod.mk>
