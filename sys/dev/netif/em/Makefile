#$FreeBSD: src/sys/modules/em/Makefile,v 1.1.2.3 2002/06/18 21:00:56 pdeuskar Exp $
#$DragonFly: src/sys/dev/netif/em/Makefile,v 1.7 2006/12/23 10:39:16 sephe Exp $

KMOD=	if_em
SRCS=	if_em.c if_em_hw.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_polling.h opt_inet.h opt_ktr.h

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}

opt_inet.h:
	echo "#define INET 1" > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
