# $FreeBSD: src/sys/modules/scsi_low/Makefile,v 1.1.2.1 2001/12/17 13:32:31 non Exp $
# $DragonFly: src/sys/bus/cam/scsi/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH: ${.CURDIR}/../../cam/scsi
KMOD=	scsi_low
SRCS=	scsi_low.c scsi_low_pisa.c 
SRCS+=  opt_ddb.h opt_scsi.h opt_cam.h bus_if.h device_if.h

.include <bsd.kmod.mk>
