#$FreeBSD: src/sys/modules/em/Makefile,v 1.1.2.3 2002/06/18 21:00:56 pdeuskar Exp $
#$DragonFly: src/sys/dev/netif/em/Makefile,v 1.10 2008/09/17 08:51:29 sephe Exp $

KMOD=	if_em
SRCS=	if_em.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_polling.h opt_ktr.h opt_serializer.h

KMODDEPS = ig_hal

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}

opt_serializer.h:
	touch ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
