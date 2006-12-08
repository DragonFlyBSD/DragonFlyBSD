# $DragonFly: src/sys/dev/disk/nata/nataraid/Makefile,v 1.2 2006/12/08 15:09:19 tgen Exp $

.PATH:	${.CURDIR}/../../../dev/disk/nata

KMOD=	nataraid
SRCS=	ata-raid.c
SRCS+=	opt_ata.h ata_if.h device_if.h bus_if.h pci_if.h

NOMAN=	

.include <bsd.kmod.mk>
