# $DragonFly: src/sys/dev/netif/bce/Makefile,v 1.1 2007/05/26 08:50:49 sephe Exp $

KMOD=	if_bce
SRCS=	if_bce.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	opt_bce.h opt_polling.h

KMODDEPS = miibus

#ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}

opt_bce.h:
	touch ${.OBJDIR}/${.TARGET}
#endif

.include <bsd.kmod.mk>
