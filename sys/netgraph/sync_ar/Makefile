# $FreeBSD: src/sys/modules/netgraph/sync_ar/Makefile,v 1.2.2.1 2002/06/17 15:11:04 jhay Exp $
 
.PATH:  ${.CURDIR}/../../dev/netif/ar
KMOD    = ng_sync_ar
SRCS    = if_ar.c if_ar_pci.c
SRCS    += device_if.h bus_if.h pci_if.h pcidevs.h opt_netgraph.h
  
opt_netgraph.h:
	echo "#define NETGRAPH" > ${.TARGET}

.include <bsd.kmod.mk>
