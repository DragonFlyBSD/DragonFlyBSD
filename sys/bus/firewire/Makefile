# $FreeBSD: src/sys/modules/firewire/firewire/Makefile,v 1.2.2.5 2003/04/28 03:29:19 simokawa Exp $
# $DragonFly: src/sys/bus/firewire/Makefile,v 1.3 2003/08/15 01:19:50 dillon Exp $

# Makefile for the IEEE1394 OHCI chipset

KMOD= firewire
SRCS	= bus_if.h device_if.h pci_if.h \
	  firewire.c firewire.h firewire_phy.h firewirereg.h \
	  fwohci.c fwohci_pci.c fwohcireg.h fwohcivar.h \
	  iec13213.h iec68113.h \
	  fwcrom.c fwdev.c fwmem.c fwmem.h fwdma.c fwdma.h

.include <bsd.kmod.mk>

