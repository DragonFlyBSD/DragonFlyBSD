# $DragonFly: src/sys/dev/netif/lnc/Makefile,v 1.1 2003/12/07 19:23:39 dillon Exp $
 
.PATH:  ${.CURDIR}/../../dev/lnc
KMOD    = if_lnc
SRCS    = if_lnc.c if_lnc_isa.c if_lnc_pci.c
SRCS    += device_if.h bus_if.h pci_if.h isa_if.h opt_inet.h
  
.include <bsd.kmod.mk>
