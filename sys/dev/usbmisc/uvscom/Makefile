# $FreeBSD: src/sys/modules/uvscom/Makefile,v 1.1.2.2 2002/08/11 20:05:20 joe Exp $
# $DragonFly: src/sys/dev/usbmisc/uvscom/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

S=	${.CURDIR}/../..
.PATH: $S/dev/usb

KMOD=	uvscom
SRCS=	uvscom.c ucomvar.h opt_usb.h device_if.h bus_if.h vnode_if.h
NOMAN=
KMODDEPS= ucom

.include <bsd.kmod.mk>
