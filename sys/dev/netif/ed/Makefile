# $DragonFly: src/sys/dev/netif/ed/Makefile,v 1.1 2004/02/13 21:15:12 joerg Exp $

.PATH:	${.CURDIR}/../../dev/fxp
KMOD	= if_ed
SRCS	= if_ed.c if_ed_isa.c if_ed_pccard.c if_ed_pci.c \
	card_if.h device_if.h bus_if.h pci_if.h isa_if.h \
	miibus_if.h \
	opt_ed.h opt_bdg.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
