# $DragonFly: src/sys/dev/netif/jme/Makefile,v 1.2 2008/09/17 08:51:29 sephe Exp $

KMOD=	if_jme
SRCS=	if_jme.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	opt_polling.h opt_jme.h

.ifndef BUILDING_WITH_KERNEL

opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}

opt_jme.h:
	echo '#define JME_RSS_DEBUG 1' > ${.OBJDIR}/${.TARGET}

.endif

.include <bsd.kmod.mk>
