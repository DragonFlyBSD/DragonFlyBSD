# $FreeBSD: src/sys/modules/aha/Makefile,v 1.4 2000/01/28 11:26:23 bde Exp $

KMOD	= aha
SRCS	= aha.c aha_isa.c ahareg.h opt_cam.h device_if.h bus_if.h use_aha.h \
	opt_scsi.h isa_if.h
CLEANFILES	= use_aha.h

use_aha.h:
	echo "#define NAHA 1" > use_aha.h

.if !defined(BUILDING_WITH_KERNEL)
opt_scsi.h:
	echo "#define SCSI_DELAY 15000" > opt_scsi.h
.endif

.include <bsd.kmod.mk>
