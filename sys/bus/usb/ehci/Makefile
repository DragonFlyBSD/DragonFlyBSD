# Makefile for EHCI module

.PATH: ${.CURDIR}/..
KMOD= ehci
SRCS= ehci.c ehci_pci.c \
	opt_usb.h \
	bus_if.h device_if.h pci_if.h

CFLAGS+= -I${.OBJDIR}/..

.include <bsd.kmod.mk>
