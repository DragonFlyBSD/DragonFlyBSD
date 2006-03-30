# $DragonFly: src/sys/bus/pccard/Makefile,v 1.2 2006/03/30 11:09:08 swildner Exp $
#
# Makefile for the PCCARD subsystem

KMOD= pccard
SRCS=	bus_if.h device_if.h pci_if.h \
	pccard.c card_if.h power_if.h\
	pccard_beep.c pccard_cis.c pccard_cis_quirks.c pccarddevs.h \
	pccardreg.h pccardvar.h \
	cis.h cardinfo.h driver.h slot.h

.include <bsd.kmod.mk>

