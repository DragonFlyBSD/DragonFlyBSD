# $DragonFly: src/sys/dev/bridge/ecc/Makefile,v 1.1 2006/10/10 18:31:03 dillon Exp $
#

KMOD = ecc
SRCS = ecc_amd8000.c ecc_e31200.c ecc_x3400.c
SRCS += device_if.h bus_if.h pci_if.h pcib_if.h
SRCS += opt_bus.h opt_pci.h
MFILES = kern/device_if.m kern/bus_if.m bus/pci/pci_if.m bus/pci/pcib_if.m

.include <bsd.kmod.mk>
