# $DragonFly: src/sys/dev/disk/nata/natapicam/Makefile,v 1.3 2006/12/08 15:09:18 tgen Exp $

.PATH:	${.CURDIR}/../../../dev/disk/nata

KMOD=	natapicam
SRCS=	atapi-cam.c
SRCS+=	opt_ata.h opt_scsi.h opt_cam.h ata_if.h device_if.h bus_if.h pci_if.h

NOMAN=	

.include <bsd.kmod.mk>
