# $FreeBSD: src/sys/modules/an/Makefile,v 1.2 2000/01/28 11:26:26 bde Exp $
# $DragonFly: src/sys/dev/netif/an/Makefile,v 1.3 2004/02/19 14:31:12 joerg Exp $

.PATH:	${.CURDIR}/../../dev/an
KMOD	= if_an
SRCS	= if_an.c if_an_pccard.c if_an_pci.c if_an_isa.c
SRCS	+= opt_inet.h device_if.h bus_if.h pci_if.h isa_if.h card_if.h

opt_inet.h:
	echo "#define INET 1" > opt_inet.h

.include <bsd.kmod.mk>
