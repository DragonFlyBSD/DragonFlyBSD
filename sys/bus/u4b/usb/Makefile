.PATH: ${.CURDIR}/.. ${.CURDIR}/../controller

KMOD=	usb

SRCS=	bus_if.h device_if.h usb_if.h usb_if.c pci_if.h \
	opt_usb.h opt_ddb.h \
	usbdevs.h usbdevs_data.h \
	usb_busdma.c usb_controller.c usb_core.c usb_debug.c \
	usb_dev.c usb_device.c usb_dynamic.c usb_error.c usb_generic.c \
	usb_handle_request.c usb_hid.c usb_hub.c usb_lookup.c usb_mbuf.c \
	usb_msctest.c usb_parse.c usb_pf.c usb_process.c usb_request.c \
	usb_transfer.c usb_util.c 

.include <bsd.kmod.mk>
