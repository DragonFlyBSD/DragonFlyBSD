# $DragonFly: src/sys/dev/netif/et/Makefile,v 1.2 2008/07/08 14:36:02 sephe Exp $

KMOD= if_et
SRCS= if_et.c
SRCS+= miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+= opt_ethernet.h
KMODDEPS= miibus

.ifndef BUILDING_WITH_KERNEL
opt_ethernet.h:
	touch ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
