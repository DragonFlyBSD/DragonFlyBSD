# $FreeBSD: src/sys/modules/mxge/Makefile,v 1.3 2008/01/15 20:34:48 gallatin Exp $

KMOD=	if_mxge
SRCS=	if_mxge.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_inet.h opt_ifpoll.h

.ifndef BUILDING_WITH_KERNEL

opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}

opt_inet.h:
	echo "#define INET 1" > ${.OBJDIR}/${.TARGET}

.endif

SUBDIR=	fw

.include <bsd.kmod.mk>
