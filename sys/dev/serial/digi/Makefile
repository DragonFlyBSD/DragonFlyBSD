# $DragonFly: src/sys/dev/serial/digi/Makefile,v 1.1 2004/12/22 08:42:47 joerg Exp $

KMOD=	digi
SRCS=	digi.c digi_pci.c digi_isa.c digi_bios.c
SRCS+=	bus_if.h pci_if.h device_if.h
SRCS+=	opt_compat.h

opt_compat.h:
	echo "#define COMPAT_43 1" > opt_compat.h

.include <bsd.kmod.mk>
