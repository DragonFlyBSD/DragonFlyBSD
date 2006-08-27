# $DragonFly: src/sys/dev/netif/nfe/Makefile,v 1.1 2006/08/27 03:28:21 sephe Exp $

KMOD=	if_nfe
SRCS=	if_nfe.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h opt_polling.h
KMODDEPS = miibus

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
