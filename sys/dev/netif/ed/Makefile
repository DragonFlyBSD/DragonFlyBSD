# $DragonFly: src/sys/dev/netif/ed/Makefile,v 1.2 2005/07/28 00:06:09 joerg Exp $

KMOD	= if_ed
SRCS	= if_ed.c if_ed_isa.c if_ed_pccard.c if_ed_pci.c \
	card_if.h device_if.h bus_if.h pci_if.h isa_if.h \
	miibus_if.h \
	opt_ed.h opt_bdg.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
