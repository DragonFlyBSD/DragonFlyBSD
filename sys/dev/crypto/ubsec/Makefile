# $FreeBSD: src/sys/modules/ubsec/Makefile,v 1.2.2.2 2003/06/05 04:13:48 sam Exp $

KMOD	= ubsec
SRCS	= ubsec.c opt_ubsec.h
SRCS   += device_if.h bus_if.h pci_if.h
SRCS   += cryptodev_if.h

.if !defined(BUILDING_WITH_KERNEL)
opt_ubsec.h:
	echo "#define UBSEC_DEBUG 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
