# $FreeBSD: src/sys/modules/rl/Makefile,v 1.6 2000/01/28 11:26:34 bde Exp $
# $DragonFly: src/sys/dev/netif/rl/Makefile,v 1.4 2005/10/24 08:06:15 sephe Exp $

KMOD	= if_rl
SRCS	= if_rl.c device_if.h bus_if.h pci_if.h
SRCS	+= opt_bdg.h opt_polling.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
