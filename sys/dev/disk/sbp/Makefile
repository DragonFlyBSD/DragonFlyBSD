# $FreeBSD: src/sys/modules/firewire/sbp/Makefile,v 1.2.2.1 2002/11/03 10:58:21 simokawa Exp $

# Makefile for the SBP-II (Serial Bus Protocol 2/SCSI over IEEE1394)

.PATH: ${.CURDIR}/../../../bus/firewire

KMOD	= sbp
SRCS	= bus_if.h device_if.h \
	  opt_cam.h \
	  sbp.c

.include <bsd.kmod.mk>

