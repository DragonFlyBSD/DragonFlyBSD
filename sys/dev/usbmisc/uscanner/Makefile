#       $FreeBSD: src/sys/modules/uscanner/Makefile,v 1.1.2.1 2000/10/31 23:16:08 n_hibma Exp $ 
#       $DragonFly: src/sys/dev/usbmisc/uscanner/Makefile,v 1.3 2004/01/31 06:56:36 dillon Exp $ 

.PATH:  	 ${.CURDIR}/../../dev/usb
KMOD		 = uscanner
SRCS		 = bus_if.h device_if.h vnode_if.h opt_usb.h uscanner.c
NOMAN		 =

CFLAGS		+= -g

.include <bsd.kmod.mk>
