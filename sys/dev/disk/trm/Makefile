# $FreeBSD: src/sys/modules/trm/Makefile,v 1.1.2.1 2002/11/19 20:47:01 cognet Exp $
# $DragonFly: src/sys/dev/disk/trm/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

.PATH: ${.CURDIR}/../../dev/trm

KMOD=	trm
SRCS=	trm.c trm.h opt_cam.h device_if.h bus_if.h \
	opt_scsi.h pci_if.h

opt_scsi.h:
	echo "#define SCSI_DELAY 15000" > opt_scsi.h

.include <bsd.kmod.mk>
