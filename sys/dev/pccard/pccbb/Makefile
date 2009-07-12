# $DragonFly: src/sys/dev/pccard/pccbb/Makefile,v 1.3 2007/08/14 14:58:44 sephe Exp $

KMOD=   cbb
SRCS=   pccbb.c pccbb_isa.c pccbb_pci.c
SRCS+=	device_if.h bus_if.h isa_if.h pci_if.h pcib_if.h power_if.h card_if.h
SRCS+=	opt_pci.h

.include <bsd.kmod.mk>
