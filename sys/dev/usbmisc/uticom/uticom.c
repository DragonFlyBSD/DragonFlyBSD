/*
 * Copyright (c) 2006-2007 Dmitry Komissaroff <dxi@mail.ru>.
 * Copyright (c) 2007 Hasso Tepper <hasso@estpak.ee>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/proc.h>
#include <sys/poll.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbcdc.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>
#include <bus/usb/usbdivar.h>
#include <bus/usb/usb_quirks.h>

#include "../ucom/ucomvar.h"

#include "uticom_fw3410.h"

SYSCTL_NODE(_hw_usb, OID_AUTO, uticom, CTLFLAG_RW, 0, "USB uticom");

#ifdef USB_DEBUG
static int	uticomdebug = 0;
SYSCTL_INT(_hw_usb_uticom, OID_AUTO, debug, CTLFLAG_RW, &uticomdebug, 0,
	   "uticom debug level");

#define DPRINTFN(n, x)	do { if (uticomdebug > (n)) kprintf x; } while (0)
#else
#define DPRINTFN(n, x)
#endif

#define DPRINTF(x) DPRINTFN(0, x)

#define	UTICOM_CONFIG_INDEX	1
#define	UTICOM_ACTIVE_INDEX	2

#define	UTICOM_IFACE_INDEX	0

/*
 * These are the maximum number of bytes transferred per frame.
 * The output buffer size cannot be increased due to the size encoding.
 */
#define UTICOM_IBUFSZ		64
#define UTICOM_OBUFSZ		64

#define UTICOM_FW_BUFSZ		16284

#define UTICOM_INTR_INTERVAL	100	/* ms */

#define UTICOM_RQ_LINE		0
/* Used to sync data0/1-toggle on reopen bulk pipe. */
#define UTICOM_RQ_SOF		1
#define UTICOM_RQ_SON		2

#define UTICOM_RQ_BAUD		3
#define UTICOM_RQ_LCR		4
#define UTICOM_RQ_FCR		5
#define UTICOM_RQ_RTS		6
#define UTICOM_RQ_DTR		7
#define UTICOM_RQ_BREAK		8
#define UTICOM_RQ_CRTSCTS	9

#define UTICOM_BRATE_REF	923077

#define UTICOM_SET_DATA_BITS(x)	(x - 5)

#define UTICOM_STOP_BITS_1	0x00
#define UTICOM_STOP_BITS_2	0x40

#define UTICOM_PARITY_NONE	0x00
#define UTICOM_PARITY_ODD	0x08
#define UTICOM_PARITY_EVEN	0x18

#define UTICOM_LCR_OVR		0x1
#define UTICOM_LCR_PTE		0x2
#define UTICOM_LCR_FRE		0x4
#define UTICOM_LCR_BRK		0x8

#define UTICOM_MCR_CTS		0x1
#define UTICOM_MCR_DSR		0x2
#define UTICOM_MCR_CD		0x4
#define UTICOM_MCR_RI		0x8

/* Structures */
struct uticom_fw_header {
	uint16_t      length;
	uint8_t       checkSum;
} __attribute__((packed));

struct uticom_buf {
	unsigned int		buf_size;
	char			*buf_buf;
	char			*buf_get;
	char			*buf_put;
};

struct	uticom_softc {
	struct ucom_softc	sc_ucom;

	int			sc_iface_number; /* interface number */

	usbd_interface_handle	sc_intr_iface;	/* interrupt interface */
	int			sc_intr_number;	/* interrupt number */
	usbd_pipe_handle	sc_intr_pipe;	/* interrupt pipe */
	u_char			*sc_intr_buf;	/* interrupt buffer */
	int			sc_isize;

	u_char			sc_dtr;		/* current DTR state */
	u_char			sc_rts;		/* current RTS state */
	u_char			sc_status;

	u_char			sc_lsr;		/* Local status register */
	u_char			sc_msr;		/* uticom status register */
};

static	usbd_status uticom_reset(struct uticom_softc *);
static	usbd_status uticom_set_crtscts(struct uticom_softc *);
static	void uticom_intr(usbd_xfer_handle, usbd_private_handle, usbd_status);

static	void uticom_set(void *, int, int, int);
static	void uticom_dtr(struct uticom_softc *, int);
static	void uticom_rts(struct uticom_softc *, int);
static	void uticom_break(struct uticom_softc *, int);
static	void uticom_get_status(void *, int, u_char *, u_char *);
#if 0 /* TODO */
static	int  uticom_ioctl(void *, int, u_long, caddr_t, int, usb_proc_ptr);
#endif
static	int  uticom_param(void *, int, struct termios *);
static	int  uticom_open(void *, int);
static	void uticom_close(void *, int);

static int uticom_download_fw(struct uticom_softc *sc, unsigned int pipeno,
			      usbd_device_handle dev, unsigned char *firmware,
			      unsigned int firmware_size);

struct ucom_callback uticom_callback = {
	uticom_get_status,
	uticom_set,
	uticom_param,
	NULL, /* uticom_ioctl, TODO */
	uticom_open,
	uticom_close,
	NULL,
	NULL
};

static const struct usb_devno uticom_devs [] = {
	{ USB_DEVICE(0x0451, 0x3410) }	/* TI TUSB3410 chip */
};

static device_probe_t uticom_match;
static device_attach_t uticom_attach;
static device_detach_t uticom_detach;

static device_method_t uticom_methods[] = {
	DEVMETHOD(device_probe, uticom_match),
	DEVMETHOD(device_attach, uticom_attach),
	DEVMETHOD(device_detach, uticom_detach),
	DEVMETHOD_END
};

static driver_t uticom_driver = {
	"ucom",
	uticom_methods,
	sizeof (struct uticom_softc)
};

DRIVER_MODULE(uticom, uhub, uticom_driver, ucom_devclass, usbd_driver_load, NULL);
MODULE_DEPEND(uticom, usb, 1, 1, 1);
MODULE_DEPEND(uticom, ucom, UCOM_MINVER, UCOM_PREFVER, UCOM_MAXVER);
MODULE_VERSION(uticom, 1);

/* Sticky DSR level sysctl handling. */
static int uticomstickdsr = 0;
static int
sysctl_hw_usb_uticom_stickdsr(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = uticomstickdsr;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);
	if (val == 0 || val == 1)
		uticomstickdsr = val;
	else
		err = EINVAL;

	return (err);
}
SYSCTL_PROC(_hw_usb_uticom, OID_AUTO, stickdsr, CTLTYPE_INT | CTLFLAG_RW,
	    0, sizeof(int), sysctl_hw_usb_uticom_stickdsr,
	    "I", "uticom sticky dsr level");

static int
uticom_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->iface != NULL)
		return (UMATCH_NONE);

	return (usb_lookup(uticom_devs, uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

static int
uticom_attach(device_t self)
{
	struct uticom_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);

	usbd_device_handle dev = uaa->device;
	struct ucom_softc *ucom;
	usb_config_descriptor_t *cdesc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usbd_status err;
	int status, i;
	usb_device_descriptor_t *dd;

	ucom = &sc->sc_ucom;
	bzero(sc, sizeof (struct uticom_softc));
	ucom->sc_dev = self;
	ucom->sc_udev = dev;
	ucom->sc_iface = uaa->iface;

	/* Initialize endpoints. */
	ucom->sc_bulkin_no = ucom->sc_bulkout_no = -1;
	sc->sc_intr_number = -1;
	sc->sc_intr_pipe = NULL;

	dd = usbd_get_device_descriptor(dev);
	DPRINTF(("%s: uticom_attach: num of configurations %d\n",
		 device_get_nameunit(self), dd->bNumConfigurations));

	/* The device without firmware has single configuration with single
	 * bulk out interface. */
	if (dd->bNumConfigurations > 1)
		goto fwload_done;
		
	/* Loading firmware. */
	DPRINTF(("%s: uticom_attach: starting loading firmware\n",
		 device_get_nameunit(self)));

	err = usbd_set_config_index(dev, UTICOM_CONFIG_INDEX, 1);
	if (err) {
		device_printf(self, "failed to set configuration: %s\n",
			      usbd_errstr(err));
		ucom->sc_dying = 1;
		return ENXIO;
	}

	/* Get the config descriptor. */
	cdesc = usbd_get_config_descriptor(ucom->sc_udev);

	if (cdesc == NULL) {
		device_printf(self, "failed to get configuration descriptor\n");
		ucom->sc_dying = 1;
		return ENXIO;
	}

	err = usbd_device2interface_handle(dev, UTICOM_IFACE_INDEX,
					   &ucom->sc_iface);
	if (err) {
		device_printf(self, "failed to get interface: %s\n",
			      usbd_errstr(err));
		ucom->sc_dying = 1;
		return ENXIO;
	}

	/* Find the bulk out interface used to upload firmware. */
	id = usbd_get_interface_descriptor(ucom->sc_iface);
	sc->sc_iface_number = id->bInterfaceNumber;

	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(ucom->sc_iface, i);
		if (ed == NULL) {
			device_printf(self,
				      "no endpoint descriptor for %d\n", i);
			ucom->sc_dying = 1;
			return ENXIO;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			ucom->sc_bulkout_no = ed->bEndpointAddress;
			DPRINTF(("%s: uticom_attach: data bulk out num: %d\n",
				 device_get_nameunit(self),
				 ed->bEndpointAddress));
		}

		if (ucom->sc_bulkout_no == -1) {
			device_printf(self, "could not find data bulk out\n");
			ucom->sc_dying = 1;
			return ENXIO;
		}
	}

	status = uticom_download_fw(sc, ucom->sc_bulkout_no, dev,
			            uticom_fw_3410, sizeof(uticom_fw_3410));

	if (status) {
		device_printf(self, "firmware download failed\n");
		ucom->sc_dying = 1;
		return ENXIO;
	} else {
		device_printf(self, "firmware download succeeded\n");
	}
		
	status = usbd_reload_device_desc(dev);
	if (status) {
		device_printf(self, "error reloading device descriptor\n");
		ucom->sc_dying = 1;
		return ENXIO;
	}

fwload_done:
	dd = usbd_get_device_descriptor(dev);
	DPRINTF(("%s: uticom_attach: num of configurations %d\n",
		 device_get_nameunit(self), dd->bNumConfigurations));

	err = usbd_set_config_index(dev, UTICOM_ACTIVE_INDEX, 1);
	if (err) {
		device_printf(self, "failed to set configuration: %s\n",
			      usbd_errstr(err));
		ucom->sc_dying = 1;
		return ENXIO;
	}

	/* Get the config descriptor. */
	cdesc = usbd_get_config_descriptor(ucom->sc_udev);
	if (cdesc == NULL) {
		device_printf(self, "failed to get configuration descriptor\n");
		ucom->sc_dying = 1;
		return ENXIO;
	}

	/* Get the interface (XXX: multiport chips are not supported yet). */
	err = usbd_device2interface_handle(dev, UTICOM_IFACE_INDEX,
					   &ucom->sc_iface);
	if (err) {
		device_printf(self, "failed to get interface: %s\n",
			      usbd_errstr(err));
		ucom->sc_dying = 1;
		return ENXIO;
	}

	/* Find the interrupt endpoints. */
	id = usbd_get_interface_descriptor(ucom->sc_iface);
	sc->sc_iface_number = id->bInterfaceNumber;

	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(ucom->sc_iface, i);
		if (ed == NULL) {
			device_printf(self,
				      "no endpoint descriptor for %d\n", i);
			ucom->sc_dying = 1;
			return ENXIO;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_intr_number = ed->bEndpointAddress;
			sc->sc_isize = UGETW(ed->wMaxPacketSize);
		}
	}

	if (sc->sc_intr_number == -1) {
		device_printf(self, "could not find interrupt in\n");
		ucom->sc_dying = 1;
		return ENXIO;
	}

	/* Keep interface for interrupt. */
	sc->sc_intr_iface = ucom->sc_iface;

	/* Find the bulk{in,out} endpoints. */
	id = usbd_get_interface_descriptor(ucom->sc_iface);
	sc->sc_iface_number = id->bInterfaceNumber;

	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(ucom->sc_iface, i);
		if (ed == NULL) {
			device_printf(self,
				      "no endpoint descriptor for %d\n", i);
			ucom->sc_dying = 1;
			return ENXIO;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			ucom->sc_bulkin_no = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			ucom->sc_bulkout_no = ed->bEndpointAddress;
		}
	}

	if (ucom->sc_bulkin_no == -1) {
		device_printf(self, "could not find data bulk in\n");
		ucom->sc_dying = 1;
		return ENXIO;
	}

	if (ucom->sc_bulkout_no == -1) {
		device_printf(self, "could not find data bulk out\n");
		ucom->sc_dying = 1;
		return ENXIO;
	}

	sc->sc_dtr = sc->sc_rts = -1;
	ucom->sc_parent = sc;
	ucom->sc_portno = UCOM_UNK_PORTNO;
	ucom->sc_ibufsize = UTICOM_IBUFSZ;
	ucom->sc_obufsize = UTICOM_OBUFSZ;
	ucom->sc_ibufsizepad = UTICOM_IBUFSZ;
	ucom->sc_opkthdrlen = 0;
	ucom->sc_callback = &uticom_callback;

	err = uticom_reset(sc);
	if (err) {
		device_printf(self, "reset failed: %s\n", usbd_errstr(err));
		ucom->sc_dying = 1;
		return ENXIO;
	}

	DPRINTF(("%s: uticom_attach: in = 0x%x, out = 0x%x, intr = 0x%x\n",
		 device_get_nameunit(self), ucom->sc_bulkin_no,
		 ucom->sc_bulkout_no, sc->sc_intr_number));

	ucom_attach(&sc->sc_ucom);
	return 0;
}

static int
uticom_detach(device_t self)
{
	struct uticom_softc *sc = device_get_softc(self);

	DPRINTF(("%s: uticom_detach: sc = %p\n",
		 device_get_nameunit(self), sc));

	if (sc->sc_intr_pipe != NULL) {
		usbd_abort_pipe(sc->sc_intr_pipe);
		usbd_close_pipe(sc->sc_intr_pipe);
		kfree(sc->sc_intr_buf, M_USBDEV);
		sc->sc_intr_pipe = NULL;
	}

	sc->sc_ucom.sc_dying = 1;
	return (ucom_detach(&sc->sc_ucom));
}

static usbd_status
uticom_reset(struct uticom_softc *sc)
{
	usb_device_request_t req;
	usbd_status err;
	device_t dev = sc->sc_ucom.sc_dev;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_SON;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
	if (err){
		device_printf(dev, "uticom_reset: %s\n", usbd_errstr(err));
		return (EIO);
	}

	DPRINTF(("%s: uticom_reset: done\n", device_get_nameunit(dev)));
	return (0);
}

static void
uticom_set(void *addr, int portno, int reg, int onoff)
{
	struct uticom_softc *sc = addr;

	switch (reg) {
	case UCOM_SET_DTR:
		uticom_dtr(sc, onoff);
		break;
	case UCOM_SET_RTS:
		uticom_rts(sc, onoff);
		break;
	case UCOM_SET_BREAK:
		uticom_break(sc, onoff);
		break;
	default:
		break;
	}
}

static void
uticom_dtr(struct uticom_softc *sc, int onoff)
{
	usb_device_request_t req;
	usbd_status err;
	device_t dev = sc->sc_ucom.sc_dev;

	DPRINTF(("%s: uticom_dtr: onoff = %d\n", device_get_nameunit(dev),
		 onoff));

	if (sc->sc_dtr == onoff)
		return;
	sc->sc_dtr = onoff;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_DTR;
	USETW(req.wValue, sc->sc_dtr ? UCDC_LINE_DTR : 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
	if (err)
		device_printf(dev, "uticom_dtr: %s\n", usbd_errstr(err));
}

static void
uticom_rts(struct uticom_softc *sc, int onoff)
{
	usb_device_request_t req;
	usbd_status err;
	device_t dev = sc->sc_ucom.sc_dev;

	DPRINTF(("%s: uticom_rts: onoff = %d\n", device_get_nameunit(dev),
		 onoff));

	if (sc->sc_rts == onoff) 
		return;
	sc->sc_rts = onoff;
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_RTS;
	USETW(req.wValue, sc->sc_rts ? UCDC_LINE_RTS : 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
	if (err)
		device_printf(dev, "uticom_rts: %s\n", usbd_errstr(err));
}

static void
uticom_break(struct uticom_softc *sc, int onoff)
{
	usb_device_request_t req;
	usbd_status err;
	device_t dev = sc->sc_ucom.sc_dev;

	DPRINTF(("%s: uticom_break: onoff = %d\n", device_get_nameunit(dev),
		 onoff));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_BREAK;
	USETW(req.wValue, onoff ? 1 : 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
	if (err)
		device_printf(dev, "uticom_break: %s\n", usbd_errstr(err));
}

static usbd_status
uticom_set_crtscts(struct uticom_softc *sc)
{
	usb_device_request_t req;
	usbd_status err;
	device_t dev = sc->sc_ucom.sc_dev;

	DPRINTF(("%s: uticom_set_crtscts: on\n", device_get_nameunit(dev)));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_CRTSCTS;
	USETW(req.wValue, 1);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
	if (err) {
		device_printf(dev, "uticom_set_crtscts: %s\n",
			      usbd_errstr(err));
		return (err);
	}

	return (USBD_NORMAL_COMPLETION);
}

static int
uticom_param(void *vsc, int portno, struct termios *t)
{
	struct uticom_softc *sc = (struct uticom_softc *)vsc;
	device_t dev = sc->sc_ucom.sc_dev;
	usb_device_request_t req;
	usbd_status err;
	uint8_t data;

	DPRINTF(("%s: uticom_param\n", device_get_nameunit(dev)));

	switch (t->c_ospeed) {
	case 1200:
	case 2400:
	case 4800:
	case 7200:
	case 9600:
	case 14400:
	case 19200:
	case 38400:
	case 57600:
	case 115200:
	case 230400:
	case 460800:
	case 921600:
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
		req.bRequest = UTICOM_RQ_BAUD;
		USETW(req.wValue, (UTICOM_BRATE_REF / t->c_ospeed));
		USETW(req.wIndex, 0);
		USETW(req.wLength, 0);

		err = usbd_do_request(sc->sc_ucom.sc_udev, &req, 0);
		if (err) {
			device_printf(dev, "uticom_param: %s\n",
				      usbd_errstr(err));
			return (EIO);
		}
		break;
	default:
		device_printf(dev, "uticom_param: unsupported baud rate %d\n",
			      t->c_ospeed);
		return (EINVAL);
	}

	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		data = UTICOM_SET_DATA_BITS(5);
		break;
	case CS6:
		data = UTICOM_SET_DATA_BITS(6);
		break;
	case CS7:
		data = UTICOM_SET_DATA_BITS(7);
		break;
	case CS8:
		data = UTICOM_SET_DATA_BITS(8);
		break;
	default:
		return (EIO);
	}

	if (ISSET(t->c_cflag, CSTOPB))
		data |= UTICOM_STOP_BITS_2;
	else
		data |= UTICOM_STOP_BITS_1;

	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			data |= UTICOM_PARITY_ODD;
		else
			data |= UTICOM_PARITY_EVEN;
	} else
		data |= UTICOM_PARITY_NONE;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_LCR;
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	USETW(req.wValue, data);

	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
	if (err) {
		device_printf(dev, "uticom_param: %s\n", usbd_errstr(err));
		return (err);
	}

	if (ISSET(t->c_cflag, CRTSCTS)) {
		err = uticom_set_crtscts(sc);
		if (err)
			return (EIO);
	} 

	return (0);
}

static int
uticom_open(void *addr, int portno)
{
	struct uticom_softc *sc = addr;
	device_t dev = sc->sc_ucom.sc_dev;
	usbd_status err;

	if (sc->sc_ucom.sc_dying)
		return (ENXIO);

	DPRINTF(("%s: uticom_open\n", device_get_nameunit(dev)));

	sc->sc_status = 0; /* clear status bit */

	if (sc->sc_intr_number != -1 && sc->sc_intr_pipe == NULL) {
		sc->sc_intr_buf = kmalloc(sc->sc_isize, M_USBDEV, M_WAITOK);
		err = usbd_open_pipe_intr(sc->sc_intr_iface, sc->sc_intr_number,
					  USBD_SHORT_XFER_OK, &sc->sc_intr_pipe,
					  sc, sc->sc_intr_buf, sc->sc_isize,
					  uticom_intr, UTICOM_INTR_INTERVAL);
		if (err) {
			device_printf(dev, "cannot open interrupt pipe "
				      "(addr %d)\n", sc->sc_intr_number);
			return (EIO);
		}
	}

	DPRINTF(("%s: uticom_open: port opened\n", device_get_nameunit(dev)));
	return (0);
}

static void
uticom_close(void *addr, int portno)
{
	struct uticom_softc *sc = addr;
	device_t dev = sc->sc_ucom.sc_dev;
	usb_device_request_t req;
	usbd_status err;

	if (sc->sc_ucom.sc_dying)
		return;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_SON;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	/* Try to reset UART part of chip. */
	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
	if (err) {
		device_printf(dev, "uticom_close: %s\n", usbd_errstr(err));
		return;
	}

	DPRINTF(("%s: uticom_close: close\n",
		 device_get_nameunit(sc->sc_ucom.sc_dev)));

	if (sc->sc_intr_pipe != NULL) {
		err = usbd_abort_pipe(sc->sc_intr_pipe);
		if (err)
			device_printf(dev, "abort interrupt pipe failed: %s\n",
				      usbd_errstr(err));
		err = usbd_close_pipe(sc->sc_intr_pipe);
		if (err)
			device_printf(dev, "close interrupt pipe failed: %s\n",
				      usbd_errstr(err));
		kfree(sc->sc_intr_buf, M_USBDEV);
		sc->sc_intr_pipe = NULL;
	}
}

static void
uticom_intr(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct uticom_softc *sc = priv;
	u_char *buf = sc->sc_intr_buf;

	if (sc->sc_ucom.sc_dying)
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			DPRINTF(("%s: uticom_intr: int status: %s\n",
				 device_get_nameunit(sc->sc_ucom.sc_dev),
				 usbd_errstr(status)));
			return;
		}

		DPRINTF(("%s: uticom_intr: abnormal status: %s\n",
			device_get_nameunit(sc->sc_ucom.sc_dev),
			usbd_errstr(status)));
		usbd_clear_endpoint_stall_async(sc->sc_intr_pipe);
		return;
	}

	if (!xfer->actlen)
		return;

	DPRINTF(("%s: xfer_length = %d\n",
		 device_get_nameunit(sc->sc_ucom.sc_dev), xfer->actlen));

	sc->sc_lsr = sc->sc_msr = 0;

	if (buf[0] == 0) {
		/* msr registers */
		if (buf[1] & UTICOM_MCR_CTS)
			sc->sc_msr |= UMSR_CTS;
		if (buf[1] & UTICOM_MCR_DSR)
			sc->sc_msr |= UMSR_DSR;
		if (buf[1] & UTICOM_MCR_CD)
			sc->sc_msr |= UMSR_DCD;		
		if (buf[1] & UTICOM_MCR_RI)
			sc->sc_msr |= UMSR_RI;		
	} else {
		/* lsr registers */
		if (buf[0] & UTICOM_LCR_OVR)
			sc->sc_lsr |= ULSR_OE;
		if (buf[0] & UTICOM_LCR_PTE)
			sc->sc_lsr |= ULSR_PE;
		if (buf[0] & UTICOM_LCR_FRE)
			sc->sc_lsr |= ULSR_FE;
		if (buf[0] & UTICOM_LCR_BRK)
			sc->sc_lsr |= ULSR_BI;	
	}
  
	if (uticomstickdsr)
		sc->sc_msr |= UMSR_DSR;

	ucom_status_change(&sc->sc_ucom);
}

static void
uticom_get_status(void *addr, int portno, u_char *lsr, u_char *msr)
{
#if 0 /* TODO */
	struct uticom_softc *sc = addr;

	DPRINTF(("uticom_get_status:\n"));

	if (lsr != NULL)
		*lsr = sc->sc_lsr;
	if (msr != NULL)
		*msr = sc->sc_msr;
#endif
	return;
}

#if 0 /* TODO */
static int
uticom_ioctl(void *addr, int portno, u_long cmd, caddr_t data, int flag,
	     usb_proc_ptr p)
{
	struct uticom_softc *sc = addr;
	int error = 0;

	if (sc->sc_ucom.sc_dying)
		return (EIO);

	DPRINTF(("uticom_ioctl: cmd = 0x%08lx\n", cmd));

	switch (cmd) {
	case TIOCNOTTY:
	case TIOCMGET:
	case TIOCMSET:
	case USB_GET_CM_OVER_DATA:
	case USB_SET_CM_OVER_DATA:
		break;

	default:
		DPRINTF(("uticom_ioctl: unknown\n"));
		error = ENOTTY;
		break;
	}

	return (error);
}
#endif

static int uticom_download_fw(struct uticom_softc *sc, unsigned int pipeno,
			      usbd_device_handle dev, unsigned char *firmware,
			      unsigned int firmware_size)
{
	int buffer_size;
	int pos;
	uint8_t cs = 0;
	uint8_t *buffer;
	usbd_status err = 0;
	usbd_xfer_handle oxfer = 0;
	u_char *obuf;
	usbd_pipe_handle pipe;
	struct uticom_fw_header *header;

	buffer_size = UTICOM_FW_BUFSZ + sizeof(struct uticom_fw_header);
	buffer = kmalloc(buffer_size, M_USBDEV, M_WAITOK);

	memcpy(buffer, firmware, firmware_size);
	memset(buffer + firmware_size, 0xff, buffer_size - firmware_size);

	for (pos = sizeof(struct uticom_fw_header); pos < buffer_size; pos++)
		cs = (uint8_t)(cs + buffer[pos]);

	header = (struct uticom_fw_header*)buffer;
	header->length = (uint16_t)(buffer_size -
				     sizeof(struct uticom_fw_header));
	header->checkSum = cs;

	DPRINTF(("%s: downloading firmware ...\n",
		 device_get_nameunit(sc->sc_ucom.sc_dev)));

	err = usbd_open_pipe(sc->sc_ucom.sc_iface, pipeno, USBD_EXCLUSIVE_USE,
			     &pipe);
	if (err) {
		device_printf(sc->sc_ucom.sc_dev, "open bulk out error "
			      "(addr %d): %s\n", pipeno, usbd_errstr(err));
		err = EIO;
		goto finish;
	}

	oxfer = usbd_alloc_xfer(dev);
	if (oxfer == NULL) {
		err = ENOMEM;
		goto finish;
	}

	obuf = usbd_alloc_buffer(oxfer, buffer_size);
	if (obuf == NULL) {
		err = ENOMEM;
		goto finish;
	}

	memcpy(obuf, buffer, buffer_size);

	usbd_setup_xfer(oxfer, pipe, (usbd_private_handle)sc, obuf, buffer_size,
			USBD_NO_COPY | USBD_SYNCHRONOUS, USBD_NO_TIMEOUT, 0);
	err = usbd_sync_transfer(oxfer);

	if (err != USBD_NORMAL_COMPLETION)
		device_printf(sc->sc_ucom.sc_dev, "uticom_download_fw: "
			      "error: %s\n", usbd_errstr(err));

finish:
	usbd_free_buffer(oxfer);
	usbd_free_xfer(oxfer);
	oxfer = NULL;
	usbd_abort_pipe(pipe);
	usbd_close_pipe(pipe);
	kfree(buffer, M_USBDEV);

	return err;	
}
