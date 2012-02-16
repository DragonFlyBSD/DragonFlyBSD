# $FreeBSD: src/sys/modules/hifn/Makefile,v 1.2.2.2 2003/06/05 04:13:47 sam Exp $

KMOD	= hifn
SRCS	= hifn7751.c opt_hifn.h
SRCS   += device_if.h bus_if.h pci_if.h
SRCS   += cryptodev_if.h

.if !defined(BUILDING_WITH_KERNEL)
opt_hifn.h:
	echo "#define HIFN_DEBUG 1" > opt_hifn.h
.endif

.include <bsd.kmod.mk>
