# $DragonFly: src/sys/dev/disk/nata/natapicam/Makefile,v 1.2 2006/12/05 19:40:43 tgen Exp $

.PATH:	${.CURDIR}/../../../dev/nata

KMOD=	natapicam
SRCS=	atapi-cam.c
SRCS+=	opt_ata.h opt_scsi.h opt_cam.h ata_if.h device_if.h bus_if.h pci_if.h

NOMAN=	

.include <bsd.kmod.mk>
