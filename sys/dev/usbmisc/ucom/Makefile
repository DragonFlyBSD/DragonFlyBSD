# $FreeBSD: src/sys/modules/ucom/Makefile,v 1.1.2.1 2002/08/08 18:45:05 joe Exp $
# $DragonFly: src/sys/dev/usbmisc/ucom/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

S=	${.CURDIR}/../..
.PATH: $S/dev/usb

KMOD=	ucom
SRCS=	ucom.c ucomvar.h opt_usb.h device_if.h bus_if.h vnode_if.h
NOMAN=

.include <bsd.kmod.mk>
