# $DragonFly: src/sys/dev/netif/vx/Makefile,v 1.1 2003/12/07 19:23:40 dillon Exp $
 
.PATH:  ${.CURDIR}/../../dev/netif/vx
KMOD    = if_vx
SRCS    = if_vx.c if_vx_eisa.c if_vx_pci.c
SRCS    += device_if.h bus_if.h pci_if.h isa_if.h
  
.include <bsd.kmod.mk>
