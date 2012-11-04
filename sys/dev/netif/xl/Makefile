# $FreeBSD: src/sys/modules/xl/Makefile,v 1.9 2000/01/28 11:26:45 bde Exp $
# $DragonFly: src/sys/dev/netif/xl/Makefile,v 1.7 2008/09/17 08:51:29 sephe Exp $

KMOD	= if_xl
SRCS	= if_xl.c device_if.h bus_if.h pci_if.h
SRCS	+= opt_ifpoll.h
SRCS	+= miibus_if.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
