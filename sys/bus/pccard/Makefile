# $DragonFly: src/sys/bus/pccard/Makefile,v 1.3 2007/07/05 12:08:53 sephe Exp $
#
# Makefile for the PCCARD subsystem

KMOD= pccard
SRCS=	bus_if.h device_if.h pci_if.h \
	pccard.c card_if.h power_if.h\
	pccard_cis.c pccard_cis_quirks.c pccarddevs.h \
	pccardreg.h pccardvar.h

.include <bsd.kmod.mk>

