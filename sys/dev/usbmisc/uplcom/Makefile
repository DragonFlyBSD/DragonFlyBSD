# $FreeBSD: src/sys/modules/uplcom/Makefile,v 1.1.2.2 2002/08/11 20:05:20 joe Exp $
# $DragonFly: src/sys/dev/usbmisc/uplcom/Makefile,v 1.4 2004/08/13 17:51:07 dillon Exp $

.PATH: ${.CURDIR}/../ucom

KMOD=	uplcom
SRCS=	uplcom.c ucomvar.h opt_usb.h device_if.h bus_if.h
NOMAN=
KMODDEPS= ucom

.include <bsd.kmod.mk>
