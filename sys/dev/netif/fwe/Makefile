# $FreeBSD: src/sys/modules/firewire/fwe/Makefile,v 1.2.2.2 2003/04/28 03:29:19 simokawa Exp $
# $DragonFly: src/sys/dev/netif/fwe/Makefile,v 1.3 2003/08/15 08:32:30 dillon Exp $

# Makefile for the fwe(4) module (Ethernet over IEEE1394)

.PATH: ${.CURDIR}/../../../bus/firewire

KMOD	= if_fwe
SRCS	= bus_if.h device_if.h \
	  opt_inet.h \
	  if_fwe.c if_fwevar.h \
	  firewire.h firewirereg.h
#CFLAGS += -DDEVICE_POLLING

.include <bsd.kmod.mk>

