# $DragonFly: src/sys/dev/netif/ipw/Attic/Makefile,v 1.3 2005/07/03 15:47:01 corecode Exp $
#
KMOD=	if_ipw
SRCS=	if_ipw.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_inet.h

#WARNS?=	6

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
