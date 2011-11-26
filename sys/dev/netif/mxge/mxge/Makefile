#$FreeBSD: src/sys/modules/mxge/mxge/Makefile,v 1.4 2009/06/23 17:42:06 gallatin Exp $

.PATH: ${.CURDIR}/..

KMOD=	if_mxge
SRCS=	if_mxge.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_inet.h

.ifndef BUILDING_WITH_KERNEL
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
