# $DragonFly: src/sys/dev/pccard/pccbb/Makefile,v 1.2 2007/07/05 12:08:54 sephe Exp $

KMOD=   cbb
SRCS=   pccbb.c pccbb_isa.c pccbb_pci.c \
	device_if.h bus_if.h isa_if.h pci_if.h pcib_if.h power_if.h card_if.h
NOMAN=

.include <bsd.kmod.mk>
