# $FreeBSD: head/sys/modules/iwn/Makefile 253911 2013-08-03 08:21:35Z uqs $

KMOD    = if_iwn
SRCS    = if_iwn.c device_if.h bus_if.h pci_if.h opt_iwn.h opt_wlan.h

SUBDIR	= fw

CFLAGS += -DIWN_DEBUG

.include <bsd.kmod.mk>
