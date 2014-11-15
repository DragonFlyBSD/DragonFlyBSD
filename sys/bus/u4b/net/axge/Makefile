# $FreeBSD: head/sys/modules/usb/axge/Makefile 258331 2013-11-19 00:37:53Z markj $

.PATH: ${.CURDIR}/..

KMOD=	if_axge
SRCS+=	if_axge.c usbdevs.h
SRCS+=	bus_if.h device_if.h miibus_if.h usb_if.h \
	opt_bus.h opt_inet.h opt_usb.h

.include <bsd.kmod.mk>
