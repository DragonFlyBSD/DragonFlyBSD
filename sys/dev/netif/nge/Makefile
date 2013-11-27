# $FreeBSD: src/sys/modules/nge/Makefile,v 1.2.2.2 2001/12/04 20:01:53 brooks Exp $

KMOD=	if_nge
SRCS=	if_nge.c miibus_if.h device_if.h bus_if.h pci_if.h pcidevs.h
SRCS+=	opt_ifpoll.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
