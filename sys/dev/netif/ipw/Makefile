# $DragonFly: src/sys/dev/netif/ipw/Attic/Makefile,v 1.2 2005/03/08 17:50:32 joerg Exp $
#
KMOD=	if_ipw
SRCS=	if_ipw.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_inet.h

#WARNS?=	6

.if !defined(BUILDING_WITH_KERNEL)
CFLAGS+=	-DALTQ

opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
