# Makefile - module build for usb.ko
#
# $DragonFly: src/sys/bus/usb/usb/Makefile,v 1.2 2004/08/13 17:51:01 dillon Exp $

S		 = ${.CURDIR}/../../..
.PATH:		 $S/bus/pci $S/bus/usb

KMOD		 = usb
SRCS		 = bus_if.h device_if.h usb_if.h usb_if.c \
		   opt_usb.h \
		   hid.c hid.h usbhid.h \
		   uhub.c \
		   usb.c usb.h \
		   usb_mem.c usb_mem.h \
		   usb_port.h \
		   usb_quirks.c usb_quirks.h \
		   usb_subr.c \
		   usbdevs.h usbdevs_data.h \
		   usbdi.c usbdi.h usbdivar.h \
		   usbdi_util.c usbdi_util.h \
		   usb_ethersubr.c

SRCS		+= uhci_pci.c uhci.c uhcireg.h uhcivar.h
SRCS		+= ohci_pci.c ohci.c ohcireg.h ohcivar.h
SRCS		+= opt_bus.h pci_if.h

NOMAN		 =

.include <bsd.kmod.mk>

regen:
	cd ${.CURDIR} && ${MAKE} -f Makefile.usbdevs regen
