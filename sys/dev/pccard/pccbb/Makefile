# $DragonFly: src/sys/dev/pccard/pccbb/Makefile,v 1.2.2.1 2007/08/14 15:11:22 sephe Exp $

KMOD=   cbb
SRCS=   pccbb.c pccbb_isa.c pccbb_pci.c
SRCS+=	device_if.h bus_if.h isa_if.h pci_if.h pcib_if.h power_if.h card_if.h
SRCS+=	opt_pci.h
NOMAN=

.ifndef BUILDING_WITH_KERNEL
opt_pci.h:
	echo "#define PCI_MAP_FIXUP 1" > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
