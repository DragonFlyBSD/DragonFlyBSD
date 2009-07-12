#       $FreeBSD: src/sys/modules/ukbd/Makefile,v 1.10 2000/01/20 13:25:18 yokota Exp $ 
#       $DragonFly: src/sys/dev/usbmisc/ukbd/Makefile,v 1.5 2005/02/18 11:41:42 corecode Exp $ 

.PATH:  	 ${.CURDIR}/../../dev/usb
KMOD		 = ukbd
SRCS		 = bus_if.h device_if.h \
		   opt_usb.h opt_kbd.h opt_ukbd.h \
		   ukbd.c

.if !defined(BUILDING_WITH_KERNEL)
opt_kbd.h:
	echo '#define KBD_INSTALL_CDEV 1' > opt_kbd.h
.endif

.include <bsd.kmod.mk>
