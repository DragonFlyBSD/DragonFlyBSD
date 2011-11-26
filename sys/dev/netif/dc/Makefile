# $FreeBSD: src/sys/modules/dc/Makefile,v 1.2 2000/01/28 11:26:28 bde Exp $
# $DragonFly: src/sys/dev/netif/dc/Makefile,v 1.5 2006/06/25 11:02:38 corecode Exp $

KMOD	= if_dc
SRCS	= if_dc.c device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h
SRCS	+= opt_polling.h

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
