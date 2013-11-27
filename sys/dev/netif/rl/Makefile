# $FreeBSD: src/sys/modules/rl/Makefile,v 1.6 2000/01/28 11:26:34 bde Exp $

KMOD	= if_rl
SRCS	= if_rl.c device_if.h bus_if.h pci_if.h pcidevs.h
SRCS	+= opt_ifpoll.h
SRCS	+= miibus_if.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
