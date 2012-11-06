# $FreeBSD: src/sys/modules/vr/Makefile,v 1.8 2000/01/28 11:26:42 bde Exp $
# $DragonFly: src/sys/dev/netif/vr/Makefile,v 1.5 2006/06/25 11:02:39 corecode Exp $

KMOD	= if_vr
SRCS	= if_vr.c device_if.h bus_if.h pci_if.h
SRCS	+= opt_ifpoll.h
SRCS	+= miibus_if.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
