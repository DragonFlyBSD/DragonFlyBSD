# $DragonFly: src/sys/dev/netif/re/Makefile,v 1.2 2005/10/24 08:06:15 sephe Exp $

KMOD	= if_re
SRCS	= if_re.c device_if.h bus_if.h pci_if.h opt_polling.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
