# $DragonFly: src/sys/bus/usb/ehci/Makefile,v 1.2 2004/03/17 19:11:26 dillon Exp $
#
# Makefile for EHCI module

.PATH: ${.CURDIR}/..
KMOD= ehci
SRCS= ehci.c ehci_pci.c \
	opt_usb.h opt_bus.h \
	bus_if.h device_if.h pci_if.h

CFLAGS+= -I${.OBJDIR}/..

.include <bsd.kmod.mk>
