# $DragonFly: src/sys/dev/disk/nata/natapicam/Makefile,v 1.1 2006/12/04 14:40:37 tgen Exp $

.PATH:	${.CURDIR}/../../../dev/nata

KMOD=	natapicam
SRCS=	atapi-cam.c
SRCS+=	opt_ata.h opt_cam.h ata_if.h device_if.h bus_if.h pci_if.h

NOMAN=	

.include <bsd.kmod.mk>
