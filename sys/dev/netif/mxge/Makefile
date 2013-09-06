# $FreeBSD: src/sys/modules/mxge/Makefile,v 1.3 2008/01/15 20:34:48 gallatin Exp $

KMOD=	if_mxge
SRCS=	if_mxge.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_inet.h

.ifndef BUILDING_WITH_KERNEL
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

SUBDIR=	fw

.include <bsd.kmod.mk>
