# $FreeBSD: src/sys/modules/uftdi/Makefile,v 1.1.2.1 2002/11/21 01:28:18 ticso Exp $
# $DragonFly: src/sys/dev/usbmisc/uftdi/Makefile,v 1.3 2004/08/13 17:51:06 dillon Exp $

S=	${.CURDIR}/../..
.PATH: $S/dev/usb

KMOD=	uftdi
SRCS=	uftdi.c uftdireg.h opt_usb.h device_if.h bus_if.h
NOMAN=

.include <bsd.kmod.mk>
