#       $FreeBSD: src/sys/modules/ugen/Makefile,v 1.7 1999/11/28 18:53:28 bde Exp $ 
#       $DragonFly: src/sys/dev/usbmisc/ugen/Makefile,v 1.4 2004/07/08 03:53:54 dillon Exp $ 

.PATH:  	 ${.CURDIR}/../../dev/usb
KMOD		 = ugen
SRCS		 = bus_if.h device_if.h vnode_if.h opt_usb.h ugen.c ugenbuf.c
NOMAN		 =

.include <bsd.kmod.mk>
