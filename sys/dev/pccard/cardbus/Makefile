# $DragonFly: src/sys/dev/pccard/cardbus/Makefile,v 1.1 2004/02/10 07:55:47 joerg Exp $

KMOD=   cardbus
SRCS=   cardbus.c cardbus_cis.c \
	device_if.h bus_if.h pci_if.h pcib_if.h power_if.h card_if.h
KMODDEPS = exca
NOMAN=

.include <bsd.kmod.mk>
