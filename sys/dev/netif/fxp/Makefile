# $FreeBSD: src/sys/modules/fxp/Makefile,v 1.9.2.3 2001/12/04 20:01:53 brooks Exp $
# $DragonFly: src/sys/dev/netif/fxp/Makefile,v 1.4 2005/10/24 08:06:15 sephe Exp $

KMOD	= if_fxp
SRCS	= if_fxp.c device_if.h bus_if.h pci_if.h miibus_if.h
SRCS	+= opt_bdg.h opt_polling.h
KMODDEPS = miibus

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
