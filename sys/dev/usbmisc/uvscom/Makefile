# $FreeBSD: src/sys/modules/uvscom/Makefile,v 1.1.2.2 2002/08/11 20:05:20 joe Exp $
# $DragonFly: src/sys/dev/usbmisc/uvscom/Makefile,v 1.3 2003/08/15 08:32:31 dillon Exp $

.PATH: ${.CURDIR}/../ucom

KMOD=	uvscom
SRCS=	uvscom.c ucomvar.h opt_usb.h device_if.h bus_if.h vnode_if.h
NOMAN=
KMODDEPS= ucom

.include <bsd.kmod.mk>
