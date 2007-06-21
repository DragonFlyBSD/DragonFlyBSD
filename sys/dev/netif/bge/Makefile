# $FreeBSD: src/sys/modules/bge/Makefile,v 1.1.2.2 2001/12/04 20:01:53 brooks Exp $
# $DragonFly: src/sys/dev/netif/bge/Makefile,v 1.8 2007/06/21 15:00:18 sephe Exp $

KMOD=	if_bge
SRCS=	if_bge.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	opt_polling.h opt_ktr.h
KMODDEPS = miibus

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
