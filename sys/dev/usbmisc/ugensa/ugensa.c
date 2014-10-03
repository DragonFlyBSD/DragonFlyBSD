/* $OpenBSD: umsm.c,v 1.15 2007/06/14 10:11:16 mbalmer Exp $ */

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

/*
 * Generic USB serial driver used for devices where hardware specific
 * don't apply or doesn't make sense (for example Qualcomm MSM EVDO, UMTS
 * and other similar communication devices).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>
#include <bus/usb/usbcdc.h>
#include <dev/usbmisc/ucom/ucomvar.h>

#ifdef UGENSA_DEBUG
static int	ugensadebug = 1;
#define DPRINTFN(n, x)  do { if (ugensadebug > (n)) kprintf x; } while (0)
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x) DPRINTFN(0, x)

#define UGENSABUFSZ		4096
#define UGENSA_INTR_INTERVAL	100	/* ms */

struct ugensa_softc {
	struct ucom_softc	 sc_ucom;
	int			 sc_iface_no;

	/* interrupt ep */
	int			 sc_intr_number;
	usbd_pipe_handle	 sc_intr_pipe;
	u_char			*sc_intr_buf;
	int			 sc_isize;

	u_char			 sc_lsr;	/* Local status register */
	u_char			 sc_msr;	/* Status register */
	u_char			 sc_dtr;	/* Current DTR state */
	u_char			 sc_rts;	/* Current RTS state */
};

static device_probe_t ugensa_match;
static device_attach_t ugensa_attach;
static device_detach_t ugensa_detach;

static int  ugensa_open(void *, int);
static void ugensa_close(void *, int);
static void ugensa_intr(usbd_xfer_handle, usbd_private_handle, usbd_status);
static void ugensa_get_status(void *, int, u_char *, u_char *);
static void ugensa_set(void *, int, int, int);

static void ugensa_e220_changemode(usbd_device_handle);

static device_method_t ugensa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, ugensa_match),
	DEVMETHOD(device_attach, ugensa_attach),
	DEVMETHOD(device_detach, ugensa_detach),
	DEVMETHOD_END
};

static driver_t ugensa_driver = {
	"ucom",
	ugensa_methods,
	sizeof (struct ugensa_softc)
};

struct ucom_callback ugensa_callback = {
	ugensa_get_status,
	ugensa_set,
	NULL,
	NULL,
	ugensa_open,
	ugensa_close,
	NULL,
	NULL
};

static const struct usb_devno ugensa_devs[] = {
	{ USB_DEVICE(0x05c6, 0x6000) }, /* Qualcomm HSDPA MSM */
	{ USB_DEVICE(0x05c6, 0x6613) }, /* Qualcomm HSDPA MSM */
	{ USB_DEVICE(0x0c88, 0x17da) }, /* Kyocera KPC650 */
	{ USB_DEVICE(0x0f3d, 0x0112) }, /* AirPrime PC5220 */
	{ USB_DEVICE(0x1199, 0x0017) }, /* Sierra Wireless EM5625 */
	{ USB_DEVICE(0x1199, 0x0018) }, /* Sierra Wireless MC5720 */
	{ USB_DEVICE(0x1199, 0x0019) }, /* Sierra Wireless AirCard 595 */
	{ USB_DEVICE(0x1199, 0x0020) }, /* Sierra Wireless MC5725 */
	{ USB_DEVICE(0x1199, 0x0021) }, /* Sierra Wireless AirCard 597E */
	{ USB_DEVICE(0x1199, 0x0112) }, /* Sierra Wireless AirCard 580 */
	{ USB_DEVICE(0x1199, 0x0120) }, /* Sierra Wireless AirCard 595U */
	{ USB_DEVICE(0x1199, 0x0218) }, /* Sierra Wireless MC5720 */
	{ USB_DEVICE(0x1199, 0x0220) }, /* Sierra Wireless MC5725 */
	{ USB_DEVICE(0x1199, 0x6802) }, /* Sierra Wireless MC8755 */
	{ USB_DEVICE(0x1199, 0x6803) }, /* Sierra Wireless MC8765 */
	{ USB_DEVICE(0x1199, 0x6804) }, /* Sierra Wireless MC8755 */
	{ USB_DEVICE(0x1199, 0x6812) }, /* Sierra Wireless MC8775 */
	{ USB_DEVICE(0x1199, 0x6813) }, /* Sierra Wireless MC8755 */
	{ USB_DEVICE(0x1199, 0x6820) }, /* Sierra Wireless AirCard 875 */
	{ USB_DEVICE(0x1199, 0x6832) }, /* Sierra Wireless MC8780 */
	{ USB_DEVICE(0x1199, 0x6833) }, /* Sierra Wireless MC8781 */
	{ USB_DEVICE(0x1199, 0x6850) }, /* Sierra Wireless AirCard 880 */
	{ USB_DEVICE(0x1199, 0x6851) }, /* Sierra Wireless AirCard 881 */
	{ USB_DEVICE(0x1199, 0x6852) }, /* Sierra Wireless AirCard 880E */
	{ USB_DEVICE(0x1199, 0x6853) }, /* Sierra Wireless AirCard 881E */
	{ USB_DEVICE(0x1199, 0x6855) }, /* Sierra Wireless AirCard 880U */
	{ USB_DEVICE(0x1199, 0x6856) }, /* Sierra Wireless AirCard 881U */
	{ USB_DEVICE(0x12d1, 0x1001) }, /* Huawei Mobile Connect */
	{ USB_DEVICE(0x12d1, 0x1003) }, /* Huawei Mobile E220 */
	{ USB_DEVICE(0x12d1, 0x1004) }, /* Huawei Mobile E220 */
	{ USB_DEVICE(0x1410, 0x1100) }, /* Novatel Wireless Merlin XS620/S640 */
	{ USB_DEVICE(0x1410, 0x1110) }, /* Novatel Wireless Merlin S620/V620 */
	{ USB_DEVICE(0x1410, 0x1120) }, /* Novatel Wireless Merlin EX720 */
	{ USB_DEVICE(0x1410, 0x1130) }, /* Novatel Wireless Merlin S720 */
	{ USB_DEVICE(0x1410, 0x1400) }, /* Novatel Wireless Merlin U730 */
	{ USB_DEVICE(0x1410, 0x1410) }, /* Novatel Wireless Merlin U740 */
	{ USB_DEVICE(0x1410, 0x1420) }, /* Novatel Wireless Expedite EU870D */
	{ USB_DEVICE(0x1410, 0x1430) }, /* Novatel Wireless Merlin XU870 */
	{ USB_DEVICE(0x1410, 0x2100) }, /* Novatel Wireless Expedite EV620 */
	{ USB_DEVICE(0x1410, 0x2110) }, /* Novatel Wireless Merlin ES620,
					   Merlin ES720, Ovation U720 */
	{ USB_DEVICE(0x1410, 0x2130) }, /* Novatel Wireless Merlin ES620 */
	{ USB_DEVICE(0x1410, 0x2410) }, /* Novatel Wireless Expedite EU740 */
	{ USB_DEVICE(0x1410, 0x4100) }, /* Novatel Wireless Ovation MC727 */
	{ USB_DEVICE(0x1410, 0x4400) }, /* Novatel Wireless Ovation MC950D */
	{ USB_DEVICE(0x16d5, 0x6501) }, /* AnyDATA ADU-E100A/D/H */
	{ USB_DEVICE(0x413c, 0x8114) }, /* Dell Wireless 5700 */
	{ USB_DEVICE(0x413c, 0x8115) }, /* Dell Wireless 5500 */
	{ USB_DEVICE(0x413c, 0x8116) }, /* Dell Wireless 5505 */
	{ USB_DEVICE(0x413c, 0x8117) }, /* Dell Wireless 5700 */
	{ USB_DEVICE(0x413c, 0x8118) }, /* Dell Wireless 5510 */
	{ USB_DEVICE(0x413c, 0x8128) }, /* Dell Wireless 5700 */
	{ USB_DEVICE(0x413c, 0x8136) }, /* Dell Wireless 5520 */
	{ USB_DEVICE(0x413c, 0x8137) }, /* Dell Wireless 5520 */
};

DRIVER_MODULE(ugensa, uhub, ugensa_driver, ucom_devclass, usbd_driver_load, NULL);
MODULE_DEPEND(ugensa, usb, 1, 1, 1);
MODULE_DEPEND(ugensa, ucom, UCOM_MINVER, UCOM_PREFVER, UCOM_MAXVER);
MODULE_VERSION(ugensa, 1);

static int
ugensa_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	/*
	 * Some devices have mass storage interfaces. What we do with these
	 * is telling them that we don't need the mass storage and then
	 * just treat them the way we should.
	 *
	 * These devices, most notably Huawei (vendor id 0x12d1) have only
	 * one interface in mass storage, and after sending them magic,
	 * they have more than one and are in the correct operating mode.
	 */

	if (uaa->vendor == 0x12d1) {
		if (uaa->nifaces > 1) {
			/*
			 * XXX: we might want to let the normal lookup handle
			 * these cases. Right now we just claim we know the
			 * device if it isn't in mass storage mode anymore.
			 */
			return UMATCH_VENDOR_IFACESUBCLASS;
		} else {
			ugensa_e220_changemode(uaa->device);
			return -1; // avoid umass to reattach (UMATCH_HIGHEST)
		}
	}

	return (usb_lookup(ugensa_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_IFACESUBCLASS : UMATCH_NONE;
}

static int
ugensa_attach(device_t self)
{
	struct ugensa_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct ucom_softc *ucom;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i;

	ucom = &sc->sc_ucom;
	bzero(sc, sizeof (struct ugensa_softc));

	ucom->sc_dev = self;
	ucom->sc_udev = uaa->device;
	ucom->sc_iface = uaa->iface;

	id = usbd_get_interface_descriptor(ucom->sc_iface);

	sc->sc_iface_no = id->bInterfaceNumber;
	ucom->sc_bulkin_no = ucom->sc_bulkout_no = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(ucom->sc_iface, i);
		if (ed == NULL) {
			device_printf(ucom->sc_dev, "no endpoint descriptor "
				      "found for %d\n", i);
			goto error;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_intr_number = ed->bEndpointAddress;
			sc->sc_isize = UGETW(ed->wMaxPacketSize);
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ucom->sc_bulkin_no = ed->bEndpointAddress;
		else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ucom->sc_bulkout_no = ed->bEndpointAddress;
	}
	if (ucom->sc_bulkin_no == -1 || ucom->sc_bulkout_no == -1) {
		device_printf(ucom->sc_dev, "missing endpoint\n");
		goto error;
	}

	sc->sc_dtr = sc->sc_rts = -1;

	ucom->sc_parent = sc;
	ucom->sc_portno = UCOM_UNK_PORTNO;
	ucom->sc_ibufsize = UGENSABUFSZ;
	ucom->sc_obufsize = UGENSABUFSZ;
	ucom->sc_ibufsizepad = UGENSABUFSZ;
	ucom->sc_opkthdrlen = 0;
	ucom->sc_callback = &ugensa_callback;

	usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, ucom->sc_udev,
			   ucom->sc_dev);

	DPRINTF(("%s: in = 0x%x, out = 0x%x\n",
		 device_get_nameunit(ucom->sc_dev), ucom->sc_bulkin_no,
		 ucom->sc_bulkout_no));

	ucom_attach(&sc->sc_ucom);

	return 0;

error:
	ucom->sc_dying = 1;
	return ENXIO;
}

static int
ugensa_detach(device_t self)
{
	struct ugensa_softc *sc = device_get_softc(self);
	int rv = 0;

	/* close the interrupt endpoint if that is opened */
	if (sc->sc_intr_pipe != NULL) {
		usbd_abort_pipe(sc->sc_intr_pipe);
		usbd_close_pipe(sc->sc_intr_pipe);
		kfree(sc->sc_intr_buf, M_USBDEV);
		sc->sc_intr_pipe = NULL;
	}

	DPRINTF(("ugensa_detach: sc=%p\n", sc));
	sc->sc_ucom.sc_dying = 1;
	rv = ucom_detach(&sc->sc_ucom);
	usbd_add_drv_event(USB_EVENT_DRIVER_DETACH, sc->sc_ucom.sc_udev,
			   sc->sc_ucom.sc_dev);

	return (rv);
}

#if 0 /* not yet */
int
ugensa_activate(struct device *self, enum devact act)
{
	struct ugensa_softc *sc = (struct ugensa_softc *)self;
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
ugensa_open(void *addr, int portno)
{
	struct ugensa_softc *sc = addr;
	int err;

	if (sc->sc_ucom.sc_dying)
		return (ENXIO);

	if (sc->sc_intr_number != -1 && sc->sc_intr_pipe == NULL) {
		sc->sc_intr_buf = kmalloc(sc->sc_isize, M_USBDEV, M_WAITOK);
		err = usbd_open_pipe_intr(sc->sc_ucom.sc_iface,
		    sc->sc_intr_number, USBD_SHORT_XFER_OK, &sc->sc_intr_pipe,
		    sc, sc->sc_intr_buf, sc->sc_isize, ugensa_intr,
		    UGENSA_INTR_INTERVAL);
		if (err) {
			device_printf(sc->sc_ucom.sc_dev,
			    "cannot open interrupt pipe (addr %d)\n",
			    sc->sc_intr_number);
			return (EIO);
		}
	}

	return (0);
}

static void
ugensa_close(void *addr, int portno)
{
	struct ugensa_softc *sc = addr;
	int err;

	if (sc->sc_ucom.sc_dying)
		return;

	if (sc->sc_intr_pipe != NULL) {
		err = usbd_abort_pipe(sc->sc_intr_pipe);
       		if (err)
			device_printf(sc->sc_ucom.sc_dev,
			    "abort interrupt pipe failed: %s\n",
			    usbd_errstr(err));
		err = usbd_close_pipe(sc->sc_intr_pipe);
		if (err)
			device_printf(sc->sc_ucom.sc_dev,
			    "close interrupt pipe failed: %s\n",
			    usbd_errstr(err));
		kfree(sc->sc_intr_buf, M_USBDEV);
		sc->sc_intr_pipe = NULL;
	}

}

static void
ugensa_intr(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct ugensa_softc *sc = priv;
	usb_cdc_notification_t *buf;
	u_char mstatus;

	buf = (usb_cdc_notification_t *)sc->sc_intr_buf;
	if (sc->sc_ucom.sc_dying)
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		device_printf(sc->sc_ucom.sc_dev,
		    "ugensa_intr: abnormal status: %s\n", usbd_errstr(status));
		usbd_clear_endpoint_stall_async(sc->sc_intr_pipe);
		return;
	}

	if (buf->bmRequestType != UCDC_NOTIFICATION) {
		DPRINTF(("%s: umsm_intr: unknown message type(0x%02x)\n",
		    sc->sc_dev.dv_xname, buf->bmRequestType));
		return;
	}

	if (buf->bNotification == UCDC_N_SERIAL_STATE) {
		/* invalid message length, discard it */
		if (UGETW(buf->wLength) != 2)
			return;
		/* XXX: sc_lsr is always 0 */
		sc->sc_lsr = sc->sc_msr = 0;
		mstatus = buf->data[0];
		if (ISSET(mstatus, UCDC_N_SERIAL_RI))
			sc->sc_msr |= UMSR_RI;
		if (ISSET(mstatus, UCDC_N_SERIAL_DSR))
			sc->sc_msr |= UMSR_DSR;
		if (ISSET(mstatus, UCDC_N_SERIAL_DCD))
			sc->sc_msr |= UMSR_DCD;
	} else if (buf->bNotification != UCDC_N_CONNECTION_SPEED_CHANGE) {
		DPRINTF(("%s: umsm_intr: unknown notify message (0x%02x)\n",
		    sc->sc_dev.dv_xname, buf->bNotification));
		return;
	}

	ucom_status_change(&sc->sc_ucom);
}

static void
ugensa_get_status(void *addr, int portno, u_char *lsr, u_char *msr)
{
	struct ugensa_softc *sc = addr;

	if (lsr != NULL)
		*lsr = sc->sc_lsr;
	if (msr != NULL)
		*msr = sc->sc_msr;
}

static void
ugensa_set(void *addr, int portno, int reg, int onoff)
{
	struct ugensa_softc *sc = addr;
	usb_device_request_t req;
	int ls;

	switch (reg) {
	case UCOM_SET_DTR:
		if (sc->sc_dtr == onoff)
			return;
		sc->sc_dtr = onoff;
		break;
	case UCOM_SET_RTS:
		if (sc->sc_rts == onoff)
			return;
		sc->sc_rts = onoff;
		break;
	default:
		return;
	}

	/* build an usb request */
	ls = (sc->sc_dtr ? UCDC_LINE_DTR : 0) |
	    (sc->sc_rts ? UCDC_LINE_RTS : 0);
	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	USETW(req.wValue, ls);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, 0);

	(void)usbd_do_request(sc->sc_ucom.sc_udev, &req, 0);
}

static void
ugensa_e220_changemode(usbd_device_handle dev)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_DEVICE;
	req.bRequest = UR_SET_FEATURE;
	USETW(req.wValue, UF_DEVICE_REMOTE_WAKEUP);
	USETW(req.wIndex, 0x2);
	USETW(req.wLength, 0);

	usbd_do_request(dev, &req, 0);
}
