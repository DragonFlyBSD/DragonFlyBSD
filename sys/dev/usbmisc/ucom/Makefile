# $FreeBSD: src/sys/modules/ucom/Makefile,v 1.1.2.1 2002/08/08 18:45:05 joe Exp $
# $DragonFly: src/sys/dev/usbmisc/ucom/Makefile,v 1.3 2004/08/13 17:51:06 dillon Exp $

S=	${.CURDIR}/../..
.PATH: $S/dev/usb

KMOD=	ucom
SRCS=	ucom.c ucomvar.h opt_usb.h device_if.h bus_if.h
NOMAN=

.include <bsd.kmod.mk>
