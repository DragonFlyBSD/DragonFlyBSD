# $FreeBSD: src/sys/modules/netgraph/sync_sr/Makefile,v 1.1.2.1 2002/06/17 15:11:05 jhay Exp $
# $DragonFly: src/sys/netgraph/sync_sr/Makefile,v 1.3 2003/08/14 23:26:44 dillon Exp $
 
.PATH:  ${.CURDIR}/../../dev/netif/sr
KMOD    = ng_sync_sr
SRCS    = if_sr.c if_sr_isa.c if_sr_pci.c
SRCS    += device_if.h bus_if.h pci_if.h isa_if.h opt_netgraph.h
  
opt_netgraph.h:
	echo "#define NETGRAPH" > opt_netgraph.h

.include <bsd.kmod.mk>
