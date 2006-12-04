# $DragonFly: src/sys/dev/disk/nata/natausb/Makefile,v 1.1 2006/12/04 14:40:37 tgen Exp $

.PATH:	${.CURDIR}/../../../dev/nata

KMOD=	natausb
SRCS=	ata-usb.c
SRCS+=	opt_ata.h opt_usb.h ata_if.h device_if.h bus_if.h pci_if.h

NOMAN=	

.include <bsd.kmod.mk>
