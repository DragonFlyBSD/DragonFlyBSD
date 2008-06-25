# $DragonFly: src/sys/dev/netif/re/Makefile,v 1.3 2008/06/25 11:02:33 sephe Exp $

KMOD	= if_re
SRCS	= if_re.c
SRCS	+= device_if.h bus_if.h pci_if.h miibus_if.h
SRCS	+= opt_polling.h opt_ethernet.h
KMODDEPS = miibus

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}

opt_ethernet.h:
	touch ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
