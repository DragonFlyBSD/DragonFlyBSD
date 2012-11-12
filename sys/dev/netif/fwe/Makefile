# $FreeBSD: src/sys/modules/firewire/fwe/Makefile,v 1.2.2.2 2003/04/28 03:29:19 simokawa Exp $
# $DragonFly: src/sys/dev/netif/fwe/Makefile,v 1.6 2007/02/04 05:26:05 swildner Exp $

# Makefile for the fwe(4) module (Ethernet over IEEE1394)

KMOD=	if_fwe
SRCS=	if_fwe.c
SRCS+=	bus_if.h device_if.h opt_inet.h opt_ifpoll.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
