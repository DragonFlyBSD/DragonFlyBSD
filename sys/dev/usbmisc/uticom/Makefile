# $DragonFly: src/sys/dev/usbmisc/uticom/Makefile,v 1.1 2007/11/07 08:31:08 hasso Exp $

.PATH: ${.CURDIR}/../ucom

KMOD=	uticom
SRCS=	uticom.c uticom_fw3410.h
SRCS+=	ucomvar.h opt_usb.h bus_if.h device_if.h

.include <bsd.kmod.mk>

