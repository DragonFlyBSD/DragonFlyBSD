# $FreeBSD: src/sys/modules/wi/Makefile,v 1.3.2.3 2002/04/28 20:20:31 imp Exp $
# $DragonFly: src/sys/dev/netif/wi/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

.PATH: ${.CURDIR}/../../dev/wi

KMOD=	if_wi
SRCS=	if_wi.c if_wi_pccard.c if_wi_pci.c wi_hostap.c \
	card_if.h device_if.h bus_if.h pci_if.h

.include <bsd.kmod.mk>
