# $DragonFly: src/sys/dev/disk/nata/natapist/Makefile,v 1.1 2006/12/04 14:40:37 tgen Exp $

.PATH:	${.CURDIR}/../../../dev/nata

KMOD=	natapist
SRCS=	atapi-tape.c
SRCS+=	opt_ata.h ata_if.h device_if.h bus_if.h pci_if.h

NOMAN=	

.include <bsd.kmod.mk>
