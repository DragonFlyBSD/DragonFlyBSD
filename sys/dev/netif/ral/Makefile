# $FreeBSD: src/sys/modules/ral/Makefile,v 1.6 2012/11/17 01:52:59 svnexp Exp $

KMOD	= if_ral
SRCS	= if_ral_pci.c rt2560.c rt2661.c rt2860.c
SRCS	+= device_if.h bus_if.h pci_if.h pcidevs.h

SUBDIR	+= fw

.include <bsd.kmod.mk>
