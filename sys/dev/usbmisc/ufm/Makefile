# $FreeBSD: src/sys/modules/ufm/Makefile,v 1.1.2.1 2002/03/04 04:01:36 alfred Exp $
# $DragonFly: src/sys/dev/usbmisc/ufm/Makefile,v 1.4 2004/08/13 17:51:06 dillon Exp $

.PATH:	${.CURDIR}/../../dev/usb ${.CURDIR}/../../../bus/usb
KMOD=	ufm
SRCS=	bus_if.h device_if.h opt_usb.h ufm.c dsbr100io.h
NOMAN=	YES

.include <bsd.kmod.mk>
