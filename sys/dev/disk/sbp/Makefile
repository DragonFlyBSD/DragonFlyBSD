# $FreeBSD: src/sys/modules/firewire/sbp/Makefile,v 1.2.2.1 2002/11/03 10:58:21 simokawa Exp $
# $DragonFly: src/sys/dev/disk/sbp/Makefile,v 1.4 2004/02/05 13:32:08 joerg Exp $

# Makefile for the SBP-II (Serial Bus Protocol 2/SCSI over IEEE1394)

.PATH: ${.CURDIR}/../../../bus/firewire

KMOD	= sbp
SRCS	= bus_if.h device_if.h \
	  opt_cam.h opt_scsi.h \
	  sbp.c sbp.h\
	  firewire.h firewirereg.h \
	  iec13213.h

.include <bsd.kmod.mk>

