KMOD=	digi
SRCS=	digi.c digi_pci.c digi_bios.c
SRCS+=	bus_if.h pci_if.h device_if.h
SRCS+=	opt_compat.h

opt_compat.h:
	echo "#define COMPAT_43 1" > ${.TARGET}

.include <bsd.kmod.mk>
