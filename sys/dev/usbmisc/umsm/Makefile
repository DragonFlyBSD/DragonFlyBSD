# $DragonFly: src/sys/dev/usbmisc/umsm/Attic/Makefile,v 1.2 2007/09/29 21:13:09 swildner Exp $

.PATH: ${.CURDIR}/../ucom

KMOD=	umsm
SRCS=	umsm.c opt_usb.h device_if.h bus_if.h
NOMAN=
KMODDEPS= ucom

.include <bsd.kmod.mk>
