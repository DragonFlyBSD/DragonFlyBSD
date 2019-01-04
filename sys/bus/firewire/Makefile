# $FreeBSD: src/sys/modules/firewire/firewire/Makefile,v 1.2.2.5 2003/04/28 03:29:19 simokawa Exp $

# Makefile for the IEEE1394 OHCI chipset

KMOD= firewire
SRCS	= bus_if.h device_if.h pci_if.h \
	  firewire.c \
	  fwohci.c fwohci_pci.c \
	  fwcrom.c fwdev.c fwmem.c fwdma.c

.include <bsd.kmod.mk>

