/*	$DragonFly: src/sys/dev/usbmisc/uslcom/uslcom.c,v 1.4 2007/08/17 06:40:19 hasso Exp $	*/
/*	$OpenBSD: uslcom.c,v 1.12 2007/06/13 06:25:03 mbalmer Exp $	*/

/*
 * Copyright (c) 2006 Jonathan Gray <jsg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/device.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>
#include <bus/usb/usbdevs.h>

#include <dev/usbmisc/ucom/ucomvar.h>

#ifdef USLCOM_DEBUG
#define DPRINTFN(n, x)  do { if (uslcomdebug > (n)) kprintf x; } while (0)
int	uslcomdebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x) DPRINTFN(0, x)

#define USLCOMBUFSZ		256
#define USLCOM_CONFIG_NO	0
#define USLCOM_IFACE_NO		0

#define USLCOM_SET_DATA_BITS(x)	(x << 8)

#define USLCOM_WRITE		0x41
#define USLCOM_READ		0xc1

#define USLCOM_UART		0x00
#define USLCOM_BAUD_RATE	0x01	
#define USLCOM_DATA		0x03
#define USLCOM_BREAK		0x05
#define USLCOM_CTRL		0x07
#define USLCOM_MODEM		0x13

#define USLCOM_UART_DISABLE	0x00
#define USLCOM_UART_ENABLE	0x01

#define USLCOM_CTRL_DTR_ON	0x0001	
#define USLCOM_CTRL_DTR_SET	0x0100
#define USLCOM_CTRL_RTS_ON	0x0002
#define USLCOM_CTRL_RTS_SET	0x0200
#define USLCOM_CTRL_CTS		0x0010
#define USLCOM_CTRL_DSR		0x0020
#define USLCOM_CTRL_DCD		0x0080


#define USLCOM_BAUD_REF		0x384000

#define USLCOM_STOP_BITS_1	0x00
#define USLCOM_STOP_BITS_2	0x02

#define USLCOM_PARITY_NONE	0x00
#define USLCOM_PARITY_ODD	0x10
#define USLCOM_PARITY_EVEN	0x20

#define USLCOM_BREAK_OFF	0x00
#define USLCOM_BREAK_ON		0x01


struct uslcom_softc {
	struct ucom_softc	sc_ucom;
	u_char			sc_msr;
	u_char			sc_lsr;
};

static void	uslcom_get_status(void *, int portno, u_char *lsr,
				  u_char *msr);
static void	uslcom_set(void *, int, int, int);
static int	uslcom_param(void *, int, struct termios *);
static int	uslcom_open(void *sc, int portno);
static void	uslcom_close(void *, int);
static void	uslcom_break(void *sc, int portno, int onoff);
static void	uslcom_set_flow_ctrl(struct uslcom_softc *sc, tcflag_t cflag,
				     tcflag_t iflag);

struct ucom_callback uslcom_callback = {
	uslcom_get_status,
	uslcom_set,
	uslcom_param,
	NULL,
	uslcom_open,
	uslcom_close,
	NULL,
	NULL,
};

static const struct usb_devno uslcom_devs[] = {
	{ USB_VENDOR_BALTECH,		USB_PRODUCT_BALTECH_CARDREADER },
	{ USB_VENDOR_DYNASTREAM,	USB_PRODUCT_DYNASTREAM_ANTDEVBOARD },
	{ USB_VENDOR_JABLOTRON,		USB_PRODUCT_JABLOTRON_PC60B },
	{ USB_VENDOR_PLX,		USB_PRODUCT_PLX_CA42 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_ARGUSISP },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CP210X_1 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CP210X_2 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CRUMB128 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_DEGREECONT },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_DESKTOPMOBILE },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_EDG1228 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_IPLINK1220 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_LIPOWSKY_HARP },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_LIPOWSKY_JTAG },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_LIPOWSKY_LIN },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_POLOLU },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_RIGBLASTER },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_RIGTALK },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_SUNNTO },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_TRACIENT_RFID },
	{ USB_VENDOR_SILABS2,		USB_PRODUCT_SILABS2_DCU11CLONE },
	{ USB_VENDOR_USI,		USB_PRODUCT_USI_MC60 }
};

static device_probe_t uslcom_match;
static device_attach_t uslcom_attach;
static device_detach_t uslcom_detach;

static device_method_t uslcom_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, uslcom_match),
	DEVMETHOD(device_attach, uslcom_attach),
	DEVMETHOD(device_detach, uslcom_detach),
	{ 0, 0 }
};

static driver_t uslcom_driver = {
	"ucom",
	uslcom_methods,
	sizeof (struct uslcom_softc)
};

DRIVER_MODULE(uslcom, uhub, uslcom_driver, ucom_devclass, usbd_driver_load, 0);
MODULE_DEPEND(uslcom, usb, 1, 1, 1);
MODULE_DEPEND(uslcom, ucom, UCOM_MINVER, UCOM_PREFVER, UCOM_MAXVER);
MODULE_VERSION(uslcom, 1);

static int
uslcom_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->iface != NULL)
		return UMATCH_NONE;

	return (usb_lookup(uslcom_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

static int
uslcom_attach(device_t self)
{
	struct uslcom_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct ucom_softc *ucom;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usbd_status error;
	char *devinfo;
	const char *devname;
	int i;

	devinfo = kmalloc(1024, M_USBDEV, M_INTWAIT);
	ucom = &sc->sc_ucom;

	bzero(sc, sizeof (struct uslcom_softc));

	usbd_devinfo(uaa->device, 0, devinfo);
	ucom->sc_dev = self;
	device_set_desc_copy(self, devinfo);

	ucom->sc_udev = uaa->device;
	ucom->sc_iface = uaa->iface;

	devname = device_get_nameunit(ucom->sc_dev);
	kprintf("%s: %s\n", devname, devinfo);
	kfree(devinfo, M_USBDEV);

	if (usbd_set_config_index(ucom->sc_udev, USLCOM_CONFIG_NO, 1) != 0) {
		kprintf("%s: could not set configuration no\n", devname);
		goto error;
        }

	/* get the first interface handle */
	error = usbd_device2interface_handle(ucom->sc_udev, USLCOM_IFACE_NO,
	    &ucom->sc_iface);
	if (error != 0) {
		kprintf("%s: could not get interface handle\n", devname);
		goto error;
	}

	id = usbd_get_interface_descriptor(ucom->sc_iface);

	ucom->sc_bulkin_no = ucom->sc_bulkout_no = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(ucom->sc_iface, i);
		if (ed == NULL) {
			kprintf("%s: no endpoint descriptor found for %d\n",
			    devname, i);
			goto error;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ucom->sc_bulkin_no = ed->bEndpointAddress;
		else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ucom->sc_bulkout_no = ed->bEndpointAddress;
	}

	if (ucom->sc_bulkin_no == -1 || ucom->sc_bulkout_no == -1) {
		kprintf("%s: missing endpoint\n", devname);
		goto error;
	}

	ucom->sc_parent = sc;
	ucom->sc_portno = UCOM_UNK_PORTNO;
	ucom->sc_ibufsize = USLCOMBUFSZ;
	ucom->sc_obufsize = USLCOMBUFSZ;
	ucom->sc_ibufsizepad = USLCOMBUFSZ;
	ucom->sc_opkthdrlen = 0;
	ucom->sc_callback = &uslcom_callback;

	usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, ucom->sc_udev,
	    ucom->sc_dev);

	DPRINTF(("uslcom: in = 0x%x, out = 0x%x, intr = 0x%x\n",
		 ucom->sc_bulkin_no, ucom->sc_bulkout_no, sc->sc_intr_number));

	ucom_attach(&sc->sc_ucom);
	return 0;

error:
	 ucom->sc_dying = 1;
	 return ENXIO;
}

static int
uslcom_detach(device_t self)
{
	struct uslcom_softc *sc = device_get_softc(self);
	int rv = 0;

	DPRINTF(("uslcom_detach: sc=%p\n", sc));
	sc->sc_ucom.sc_dying = 1;
	rv = ucom_detach(&sc->sc_ucom);
	usbd_add_drv_event(USB_EVENT_DRIVER_DETACH, sc->sc_ucom.sc_udev,
			   sc->sc_ucom.sc_dev);

	return (rv);
}

#if 0 /* not yet */
int
uslcom_activate(struct device *self, enum devact act)
{
	struct uslcom_softc *sc = (struct uslcom_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_ACTIVATE:
		break;

	case DVACT_DEACTIVATE:
		if (sc->sc_subdev != NULL)
			rv = config_deactivate(sc->sc_subdev);
		sc->sc_dying = 1;
		break;
	}
	return (rv);
}
#endif

static int
uslcom_open(void *vsc, int portno)
{
	struct uslcom_softc *sc = vsc;
	usb_device_request_t req;
	usbd_status err;

	if (sc->sc_ucom.sc_dying)
		return (EIO);

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_UART;
	USETW(req.wValue, USLCOM_UART_ENABLE);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
	if (err)
		return (EIO);

	return (0);
}

static void
uslcom_close(void *vsc, int portno)
{
	struct uslcom_softc *sc = vsc;
	usb_device_request_t req;

	if (sc->sc_ucom.sc_dying)
		return;

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_UART;
	USETW(req.wValue, USLCOM_UART_DISABLE);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
}

static void
uslcom_set(void *vsc, int portno, int reg, int onoff)
{
	struct uslcom_softc *sc = vsc;
	usb_device_request_t req;
	int ctl;

	switch (reg) {
	case UCOM_SET_DTR:
		ctl = onoff ? USLCOM_CTRL_DTR_ON : 0;
		ctl |= USLCOM_CTRL_DTR_SET;
		break;
	case UCOM_SET_RTS:
		ctl = onoff ? USLCOM_CTRL_RTS_ON : 0;
		ctl |= USLCOM_CTRL_RTS_SET;
		break;
	case UCOM_SET_BREAK:
		uslcom_break(sc, portno, onoff);
		return;
	default:
		return;
	}
	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_CTRL;
	USETW(req.wValue, ctl);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
}

static int
uslcom_param(void *vsc, int portno, struct termios *t)
{
	struct uslcom_softc *sc = (struct uslcom_softc *)vsc;
	usbd_status err;
	usb_device_request_t req;
	int data;

	switch (t->c_ospeed) {
	case 600:
	case 1200:
	case 1800:
	case 2400:
	case 4800:
	case 9600:
	case 19200:
	case 38400:
	case 57600:
	case 115200:
	case 460800:
	case 921600:
		req.bmRequestType = USLCOM_WRITE;
		req.bRequest = USLCOM_BAUD_RATE;
		USETW(req.wValue, USLCOM_BAUD_REF / t->c_ospeed);
		USETW(req.wIndex, portno);
		USETW(req.wLength, 0);
		err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
		if (err)
			return (EIO);
		break;
	default:
		return (EINVAL);
	}

	if (ISSET(t->c_cflag, CSTOPB))
		data = USLCOM_STOP_BITS_2;
	else
		data = USLCOM_STOP_BITS_1;
	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			data |= USLCOM_PARITY_ODD;
		else
			data |= USLCOM_PARITY_EVEN;
	} else
		data |= USLCOM_PARITY_NONE;
	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		data |= USLCOM_SET_DATA_BITS(5);
		break;
	case CS6:
		data |= USLCOM_SET_DATA_BITS(6);
		break;
	case CS7:
		data |= USLCOM_SET_DATA_BITS(7);
		break;
	case CS8:
		data |= USLCOM_SET_DATA_BITS(8);
		break;
	}

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_DATA;
	USETW(req.wValue, data);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
	if (err)
		return (EIO);

	uslcom_set_flow_ctrl(sc, t->c_cflag, t->c_iflag);

	return (0);
}

static void
uslcom_get_status(void *vsc, int portno, u_char *lsr, u_char *msr)
{
	struct uslcom_softc *sc = vsc;
	
	if (msr != NULL)
		*msr = sc->sc_msr;
	if (lsr != NULL)
		*lsr = sc->sc_lsr;
}

static void
uslcom_break(void *vsc, int portno, int onoff)
{
	struct uslcom_softc *sc = vsc;
	usb_device_request_t req;
	int brk = onoff ? USLCOM_BREAK_ON : USLCOM_BREAK_OFF;	

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_BREAK;
	USETW(req.wValue, brk);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
}

static void
uslcom_set_flow_ctrl(struct uslcom_softc *sc, tcflag_t cflag, tcflag_t iflag)
{
	uint8_t modemdata[16];
	usb_device_request_t req;
	usbd_status err;

	req.bmRequestType = USLCOM_READ;
	req.bRequest = USLCOM_MODEM;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 16);

	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, modemdata);
	if (err)
		kprintf("uslcom_set_flow: %s\n", usbd_errstr(err));

	if (ISSET(cflag, CRTSCTS)) {
		modemdata[0] &= ~0x7b;
		modemdata[0] |= 0x09;
		modemdata[4] = 0x80;
	} else {
		modemdata[0] &= ~0x7b;
		modemdata[0] |= 0x01;
		modemdata[4] = 0x40;
	}

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_MODEM;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 16);

	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, modemdata);
	if (err)
		kprintf("uslcom_set_flow: %s\n", usbd_errstr(err));
}

