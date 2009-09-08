# $DragonFly: src/sys/dev/netif/ed/Makefile,v 1.3 2006/06/25 11:02:38 corecode Exp $

KMOD	= if_ed
SRCS	= if_ed.c if_ed_isa.c if_ed_pccard.c if_ed_pci.c \
	card_if.h device_if.h bus_if.h pci_if.h isa_if.h \
	miibus_if.h \
	opt_ed.h

.include <bsd.kmod.mk>
