#$FreeBSD: src/sys/modules/em/Makefile,v 1.1.2.3 2002/06/18 21:00:56 pdeuskar Exp $
#$DragonFly: src/sys/dev/netif/em/Makefile,v 1.4 2005/10/24 08:06:15 sephe Exp $

KMOD=	if_em
SRCS=	if_em.c if_em_hw.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_bdg.h opt_polling.h

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
