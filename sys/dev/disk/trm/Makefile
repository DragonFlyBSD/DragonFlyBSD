# $FreeBSD: src/sys/modules/trm/Makefile,v 1.1.2.1 2002/11/19 20:47:01 cognet Exp $

KMOD=	trm
SRCS=	trm.c opt_cam.h device_if.h bus_if.h pci_if.h

.if !defined(BUILDING_WITH_KERNEL)
opt_scsi.h:
	echo "#define SCSI_DELAY 15000" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
