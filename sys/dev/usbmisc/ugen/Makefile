#       $FreeBSD: src/sys/modules/ugen/Makefile,v 1.7 1999/11/28 18:53:28 bde Exp $ 
#       $DragonFly: src/sys/dev/usbmisc/ugen/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $ 

MAINTAINER	 = n_hibma@freebsd.org

.PATH:  	 ${.CURDIR}/../../dev/usb
KMOD		 = ugen
SRCS		 = bus_if.h device_if.h vnode_if.h opt_usb.h ugen.c
NOMAN		 =

.include <bsd.kmod.mk>
