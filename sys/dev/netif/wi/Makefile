# $FreeBSD: src/sys/modules/wi/Makefile,v 1.3.2.3 2002/04/28 20:20:31 imp Exp $
# $DragonFly: src/sys/dev/netif/wi/Makefile,v 1.4 2005/10/24 08:06:15 sephe Exp $

KMOD=	if_wi
SRCS=	if_wi.c if_wi_pccard.c if_wi_pci.c \
	opt_wi.h opt_polling.h \
	card_if.h device_if.h bus_if.h pci_if.h

.ifndef BUILDING_WITH_KERNEL
opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}
.endif

opt_wi.h:
	echo '#define WI_SYMBOL_FIRMWARE 1' > ${.OBJDIR}/opt_wi.h

.include <bsd.kmod.mk>
