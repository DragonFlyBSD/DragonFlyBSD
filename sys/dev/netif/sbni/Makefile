# $FreeBSD: src/sys/modules/sbni/Makefile,v 1.1.2.1 2001/12/19 20:59:29 fjoe Exp $
# $DragonFly: src/sys/dev/netif/sbni/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH: ${.CURDIR}/../../dev/sbni

KMOD=	if_sbni
SRCS=	if_sbni.c if_sbni_pci.c

SRCS+=	bus_if.h device_if.h isa_if.h pci_if.h

.include <bsd.kmod.mk>
