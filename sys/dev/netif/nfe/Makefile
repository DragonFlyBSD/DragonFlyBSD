# $DragonFly: src/sys/dev/netif/nfe/Makefile,v 1.2 2008/06/24 13:55:17 sephe Exp $

KMOD=	if_nfe
SRCS=	if_nfe.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	opt_polling.h opt_ethernet.h
KMODDEPS = miibus

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}

opt_ethernet.h:
	touch ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
