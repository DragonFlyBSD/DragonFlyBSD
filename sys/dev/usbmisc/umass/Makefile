#       $FreeBSD: src/sys/modules/umass/Makefile,v 1.7.2.1 2000/04/01 19:33:33 n_hibma Exp $ 
#       $DragonFly: src/sys/dev/usbmisc/umass/Makefile,v 1.3 2004/01/31 06:56:33 dillon Exp $ 

.PATH:  	 ${.CURDIR}/../../dev/usb
KMOD		 = umass
SRCS		 = bus_if.h device_if.h \
		   opt_usb.h opt_cam.h opt_scsi.h \
		   umass.c
NOMAN		 =

.include <bsd.kmod.mk>
