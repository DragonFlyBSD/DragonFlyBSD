# $FreeBSD: src/sys/modules/bge/Makefile,v 1.1.2.2 2001/12/04 20:01:53 brooks Exp $

KMOD=	if_bge
SRCS=	if_bge.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h pcidevs.h
SRCS+=	opt_ifpoll.h opt_ktr.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
