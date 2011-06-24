/* $OpenBSD: src/sys/dev/usb/moscom.c,v 1.11 2007/10/11 18:33:14 deraadt Exp $ */

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

#include <dev/usbmisc/ucom/ucomvar.h>

#define MOSCOMBUFSZ		256
#define MOSCOM_CONFIG_NO	0
#define MOSCOM_IFACE_NO		0

#define MOSCOM_READ		0x0d
#define MOSCOM_WRITE		0x0e
#define MOSCOM_UART_REG		0x0300
#define MOSCOM_VEND_REG		0x0000

#define MOSCOM_TXBUF		0x00	/* Write */
#define MOSCOM_RXBUF		0x00	/* Read */
#define MOSCOM_INT		0x01
#define MOSCOM_FIFO		0x02	/* Write */
#define MOSCOM_ISR		0x02	/* Read */
#define MOSCOM_LCR		0x03
#define MOSCOM_MCR		0x04
#define MOSCOM_LSR		0x05
#define MOSCOM_MSR		0x06
#define MOSCOM_SCRATCH		0x07
#define MOSCOM_DIV_LO		0x08
#define MOSCOM_DIV_HI		0x09
#define MOSCOM_EFR		0x0a
#define	MOSCOM_XON1		0x0b
#define MOSCOM_XON2		0x0c
#define MOSCOM_XOFF1		0x0d
#define MOSCOM_XOFF2		0x0e

#define MOSCOM_BAUDLO		0x00
#define MOSCOM_BAUDHI		0x01

#define MOSCOM_INT_RXEN		0x01
#define MOSCOM_INT_TXEN		0x02
#define MOSCOM_INT_RSEN		0x04	
#define MOSCOM_INT_MDMEM	0x08
#define MOSCOM_INT_SLEEP	0x10
#define MOSCOM_INT_XOFF		0x20
#define MOSCOM_INT_RTS		0x40	

#define MOSCOM_FIFO_EN		0x01
#define MOSCOM_FIFO_RXCLR	0x02
#define MOSCOM_FIFO_TXCLR	0x04
#define MOSCOM_FIFO_DMA_BLK	0x08
#define MOSCOM_FIFO_TXLVL_MASK	0x30
#define MOSCOM_FIFO_TXLVL_8	0x00
#define MOSCOM_FIFO_TXLVL_16	0x10
#define MOSCOM_FIFO_TXLVL_32	0x20
#define MOSCOM_FIFO_TXLVL_56	0x30
#define MOSCOM_FIFO_RXLVL_MASK	0xc0
#define MOSCOM_FIFO_RXLVL_8	0x00
#define MOSCOM_FIFO_RXLVL_16	0x40
#define MOSCOM_FIFO_RXLVL_56	0x80
#define MOSCOM_FIFO_RXLVL_80	0xc0

#define MOSCOM_ISR_MDM		0x00
#define MOSCOM_ISR_NONE		0x01
#define MOSCOM_ISR_TX		0x02
#define MOSCOM_ISR_RX		0x04
#define MOSCOM_ISR_LINE		0x06
#define MOSCOM_ISR_RXTIMEOUT	0x0c
#define MOSCOM_ISR_RX_XOFF	0x10
#define MOSCOM_ISR_RTSCTS	0x20
#define MOSCOM_ISR_FIFOEN	0xc0

#define MOSCOM_LCR_DBITS(x)	(x - 5)
#define MOSCOM_LCR_STOP_BITS_1	0x00
#define MOSCOM_LCR_STOP_BITS_2	0x04	/* 2 if 6-8 bits/char or 1.5 if 5 */
#define MOSCOM_LCR_PARITY_NONE	0x00
#define MOSCOM_LCR_PARITY_ODD	0x08
#define MOSCOM_LCR_PARITY_EVEN	0x18
#define MOSCOM_LCR_BREAK	0x40
#define MOSCOM_LCR_DIVLATCH_EN	0x80

#define MOSCOM_MCR_DTR		0x01
#define MOSCOM_MCR_RTS		0x02
#define MOSCOM_MCR_LOOP		0x04
#define MOSCOM_MCR_INTEN	0x08
#define MOSCOM_MCR_LOOPBACK	0x10
#define MOSCOM_MCR_XONANY	0x20
#define MOSCOM_MCR_IRDA_EN	0x40
#define MOSCOM_MCR_BAUD_DIV4	0x80

#define MOSCOM_LSR_RXDATA	0x01
#define MOSCOM_LSR_RXOVER	0x02
#define MOSCOM_LSR_RXPAR_ERR	0x04
#define MOSCOM_LSR_RXFRM_ERR	0x08
#define MOSCOM_LSR_RXBREAK	0x10
#define MOSCOM_LSR_TXEMPTY	0x20
#define MOSCOM_LSR_TXALLEMPTY	0x40
#define MOSCOM_LSR_TXFIFO_ERR	0x80

#define MOSCOM_MSR_CTS_CHG	0x01
#define MOSCOM_MSR_DSR_CHG	0x02
#define MOSCOM_MSR_RI_CHG	0x04
#define MOSCOM_MSR_CD_CHG	0x08
#define MOSCOM_MSR_CTS		0x10
#define MOSCOM_MSR_RTS		0x20
#define MOSCOM_MSR_RI		0x40
#define MOSCOM_MSR_CD		0x80

#define MOSCOM_BAUD_REF		115200

struct moscom_softc {
	struct ucom_softc	sc_ucom;
	u_char			sc_msr;
	u_char			sc_lsr;
	u_char			sc_lcr;
};

static void	moscom_get_status(void *, int, u_char *, u_char *);
static void	moscom_set(void *, int, int, int);
static int	moscom_param(void *, int, struct termios *);
static int	moscom_open(void *, int);
static int	moscom_cmd(struct moscom_softc *, int, int);	

struct ucom_callback moscom_callback = {
	moscom_get_status,
	moscom_set,
	moscom_param,
	NULL,
	moscom_open,
	NULL,
	NULL,
	NULL,
};

static const struct usb_devno moscom_devs[] = {
	{ USB_DEVICE(0x9710, 0x7703) }  /* MosChip MCS7703 serial port */
};

static device_probe_t moscom_match;
static device_attach_t moscom_attach;
static device_detach_t moscom_detach;

static device_method_t moscom_methods[] = {
	DEVMETHOD(device_probe, moscom_match),
	DEVMETHOD(device_attach, moscom_attach),
	DEVMETHOD(device_detach, moscom_detach),
	{ 0, 0 }
};

static driver_t moscom_driver = {
	"ucom",
	moscom_methods,
	sizeof (struct moscom_softc)
};

DRIVER_MODULE(moscom, uhub, moscom_driver, ucom_devclass, usbd_driver_load, NULL);
MODULE_DEPEND(moscom, usb, 1, 1, 1);
MODULE_DEPEND(moscom, ucom, UCOM_MINVER, UCOM_PREFVER, UCOM_MAXVER);
MODULE_VERSION(moscom, 1);

static int
moscom_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->iface != NULL)
		return UMATCH_NONE;

	return (usb_lookup(moscom_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

static int
moscom_attach(device_t self)
{
	struct moscom_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct ucom_softc *ucom;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usbd_status error;
	int i;

	bzero(sc, sizeof (struct moscom_softc));
	ucom = &sc->sc_ucom;
	ucom->sc_dev = self;
	ucom->sc_udev = uaa->device;
	ucom->sc_iface = uaa->iface;

	if (usbd_set_config_index(ucom->sc_udev, MOSCOM_CONFIG_NO, 1) != 0) {
		device_printf(ucom->sc_dev, "could not set configuration no\n");
		ucom->sc_dying = 1;
		return ENXIO;
	}

	/* get the first interface handle */
	error = usbd_device2interface_handle(ucom->sc_udev, MOSCOM_IFACE_NO,
	    &ucom->sc_iface);
	if (error != 0) {
		device_printf(ucom->sc_dev, "could not get interface handle\n");
		ucom->sc_dying = 1;
		return ENXIO;
	}

	id = usbd_get_interface_descriptor(ucom->sc_iface);

	ucom->sc_bulkin_no = ucom->sc_bulkout_no = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(ucom->sc_iface, i);
		if (ed == NULL) {
			device_printf(ucom->sc_dev, "no endpoint descriptor "
				      "found for %d\n", i);
			ucom->sc_dying = 1;
			return ENXIO;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ucom->sc_bulkin_no = ed->bEndpointAddress;
		else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ucom->sc_bulkout_no = ed->bEndpointAddress;
	}

	if (ucom->sc_bulkin_no == -1 || ucom->sc_bulkout_no == -1) {
		device_printf(ucom->sc_dev, "missing endpoint\n");
		ucom->sc_dying = 1;
		return ENXIO;
	}

	ucom->sc_parent = sc;
	ucom->sc_portno = UCOM_UNK_PORTNO;
	ucom->sc_ibufsize = MOSCOMBUFSZ;
	ucom->sc_obufsize = MOSCOMBUFSZ;
	ucom->sc_ibufsizepad = MOSCOMBUFSZ;
	ucom->sc_opkthdrlen = 0;
	ucom->sc_callback = &moscom_callback;

	usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, ucom->sc_udev,
			   ucom->sc_dev);

	return 0;
}

static int
moscom_detach(device_t self)
{
	struct moscom_softc *sc = device_get_softc(self);
	int rv = 0;

	sc->sc_ucom.sc_dying = 1;
	rv = ucom_detach(&sc->sc_ucom);
	usbd_add_drv_event(USB_EVENT_DRIVER_DETACH, sc->sc_ucom.sc_udev,
			   sc->sc_ucom.sc_dev);

	return (rv);
}

#if 0 /* endif */
int
moscom_activate(struct device *self, enum devact act)
{
	struct moscom_softc *sc = (struct moscom_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_ACTIVATE:
		break;

	case DVACT_DEACTIVATE:
		if (sc->sc_subdev != NULL)
			rv = config_deactivate(sc->sc_subdev);
		sc->sc_ucom.sc_dying = 1;
		break;
	}
	return (rv);
}
#endif

static int
moscom_open(void *vsc, int portno)
{
	struct moscom_softc *sc = vsc;
	usb_device_request_t req;

	if (sc->sc_ucom.sc_dying)
		return (EIO);

	/* Purge FIFOs or odd things happen */
	if (moscom_cmd(sc, MOSCOM_FIFO, 0x00) != 0)
		return (EIO);
	
	if (moscom_cmd(sc, MOSCOM_FIFO, MOSCOM_FIFO_EN |
	    MOSCOM_FIFO_RXCLR | MOSCOM_FIFO_TXCLR |
	    MOSCOM_FIFO_DMA_BLK | MOSCOM_FIFO_RXLVL_MASK) != 0) 
		return (EIO);

	/* Magic tell device we're ready for data command */
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MOSCOM_WRITE;
	USETW(req.wValue, 0x08);
	USETW(req.wIndex, MOSCOM_INT);
	USETW(req.wLength, 0);
	if (usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL) != 0)
		return (EIO);

	return (0);
}

static void
moscom_set(void *vsc, int portno, int reg, int onoff)
{
	struct moscom_softc *sc = vsc;
	int val;

	switch (reg) {
	case UCOM_SET_DTR:
		val = onoff ? MOSCOM_MCR_DTR : 0;
		break;
	case UCOM_SET_RTS:
		val = onoff ? MOSCOM_MCR_RTS : 0;
		break;
	case UCOM_SET_BREAK:
		val = sc->sc_lcr;
		if (onoff)
			val |= MOSCOM_LCR_BREAK;
		moscom_cmd(sc, MOSCOM_LCR, val);
		return;
	default:
		return;
	}

	moscom_cmd(sc, MOSCOM_MCR, val);
}

static int
moscom_param(void *vsc, int portno, struct termios *t)
{
	struct moscom_softc *sc = (struct moscom_softc *)vsc;
	int data;

	if (t->c_ospeed <= 0 || t->c_ospeed > 115200)
		return (EINVAL);

	data = MOSCOM_BAUD_REF / t->c_ospeed;

	if (data == 0 || data > 0xffff)
		return (EINVAL);

	moscom_cmd(sc, MOSCOM_LCR, MOSCOM_LCR_DIVLATCH_EN);
	moscom_cmd(sc, MOSCOM_BAUDLO, data & 0xFF);
	moscom_cmd(sc, MOSCOM_BAUDHI, (data >> 8) & 0xFF);

	if (ISSET(t->c_cflag, CSTOPB))
		data = MOSCOM_LCR_STOP_BITS_2;
	else
		data = MOSCOM_LCR_STOP_BITS_1;
	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			data |= MOSCOM_LCR_PARITY_ODD;
		else
			data |= MOSCOM_LCR_PARITY_EVEN;
	} else
		data |= MOSCOM_LCR_PARITY_NONE;
	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		data |= MOSCOM_LCR_DBITS(5);
		break;
	case CS6:
		data |= MOSCOM_LCR_DBITS(6);
		break;
	case CS7:
		data |= MOSCOM_LCR_DBITS(7);
		break;
	case CS8:
		data |= MOSCOM_LCR_DBITS(8);
		break;
	}

	sc->sc_lcr = data;
	moscom_cmd(sc, MOSCOM_LCR, sc->sc_lcr);

#if 0
	/* XXX flow control */
	if (ISSET(t->c_cflag, CRTSCTS))
		/*  rts/cts flow ctl */
	} else if (ISSET(t->c_iflag, IXON|IXOFF)) {
		/*  xon/xoff flow ctl */
	} else {
		/* disable flow ctl */
	}
#endif

	return (0);
}

static void
moscom_get_status(void *vsc, int portno, u_char *lsr, u_char *msr)
{
	struct moscom_softc *sc = vsc;
	
	if (msr != NULL)
		*msr = sc->sc_msr;
	if (lsr != NULL)
		*lsr = sc->sc_lsr;
}

static int
moscom_cmd(struct moscom_softc *sc, int reg, int val)
{
	usb_device_request_t req;
	usbd_status err;
	
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MOSCOM_WRITE;
	USETW(req.wValue, val + MOSCOM_UART_REG);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);
	err = usbd_do_request(sc->sc_ucom.sc_udev, &req, NULL);
	if (err)
		return (EIO);
	else
		return (0);
}
