# $FreeBSD: src/sys/modules/an/Makefile,v 1.2 2000/01/28 11:26:26 bde Exp $

KMOD	= if_an
SRCS	= if_an.c if_an_pccard.c if_an_pci.c if_an_isa.c
SRCS	+= opt_inet.h device_if.h bus_if.h pci_if.h isa_if.h card_if.h
SRCS	+= pccarddevs.h pcidevs.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
