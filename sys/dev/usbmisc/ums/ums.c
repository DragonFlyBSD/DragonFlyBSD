/*
 * $FreeBSD: src/sys/dev/usb/ums.c,v 1.64 2003/11/09 09:17:22 tanimura Exp $
 */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * HID spec: http://www.usb.org/developers/data/devclass/hid1_1.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <sys/vnode.h>
#include <sys/event.h>
#include <sys/sysctl.h>
#include <sys/devfs.h>
#include <sys/thread2.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbhid.h>

#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>
#include <bus/usb/usb_quirks.h>
#include <bus/usb/hid.h>

#include <sys/mouse.h>

#ifdef USB_DEBUG
#define DPRINTF(x)	if (umsdebug) kprintf x
#define DPRINTFN(n,x)	if (umsdebug>(n)) kprintf x
int	umsdebug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, ums, CTLFLAG_RW, 0, "USB ums");
SYSCTL_INT(_hw_usb_ums, OID_AUTO, debug, CTLFLAG_RW,
	   &umsdebug, 0, "ums debug level");
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define UMSUNIT(s)	(minor(s)&0x1f)

#define MS_TO_TICKS(ms) ((ms) * hz / 1000)

#define QUEUE_BUFSIZE	400	/* MUST be divisible by 5 _and_ 8 */

struct ums_softc {
	device_t sc_dev;		/* base device */
	cdev_t	 sc_cdev;
	usbd_interface_handle sc_iface;	/* interface */
	usbd_pipe_handle sc_intrpipe;	/* interrupt pipe */
	int sc_ep_addr;

	u_char *sc_ibuf;
	u_int8_t sc_iid;
	int sc_isize;
	struct hid_location sc_loc_x, sc_loc_y, sc_loc_z;
	struct hid_location *sc_loc_btn;

	struct callout sc_timeout;	/* for spurious button ups */

	int sc_enabled;
	int sc_disconnected;	/* device is gone */

	int flags;		/* device configuration */
#define UMS_Z		0x01	/* z direction available */
#define UMS_SPUR_BUT_UP	0x02	/* spurious button up events */
	int nbuttons;
#define MAX_BUTTONS	31	/* must not exceed size of sc_buttons */

	u_char		qbuf[QUEUE_BUFSIZE];	/* must be divisable by 3&4 */
	u_char		dummy[100];	/* XXX just for safety and for now */
	int		qcount, qhead, qtail;
	mousehw_t	hw;
	mousemode_t	mode;
	mousestatus_t	status;

	int		state;
#	  define	UMS_ASLEEP	0x01	/* readFromDevice is waiting */
	struct kqinfo	rkq;		/* process waiting in select/poll/kq */
};

#define MOUSE_FLAGS_MASK (HIO_CONST|HIO_RELATIVE)
#define MOUSE_FLAGS (HIO_RELATIVE)

static void ums_intr(usbd_xfer_handle xfer,
			  usbd_private_handle priv, usbd_status status);

static void ums_add_to_queue(struct ums_softc *sc,
				int dx, int dy, int dz, int buttons);
static void ums_add_to_queue_timeout(void *priv);

static int  ums_enable(void *);
static void ums_disable(void *);

static d_open_t  ums_open;
static d_close_t ums_close;
static d_read_t  ums_read;
static d_ioctl_t ums_ioctl;
static d_kqfilter_t ums_kqfilter;

static void ums_filt_detach(struct knote *);
static int ums_filt(struct knote *, long);

static struct dev_ops ums_ops = {
	{ "ums", 0, 0 },
	.d_open =	ums_open,
	.d_close =	ums_close,
	.d_read =	ums_read,
	.d_ioctl =	ums_ioctl,
	.d_kqfilter =	ums_kqfilter
};

static device_probe_t ums_match;
static device_attach_t ums_attach;
static device_detach_t ums_detach;

static devclass_t ums_devclass;

static kobj_method_t ums_methods[] = {
	DEVMETHOD(device_probe, ums_match),
	DEVMETHOD(device_attach, ums_attach),
	DEVMETHOD(device_detach, ums_detach),
	{0,0}
};

static driver_t ums_driver = {
	"ums",
	ums_methods,
	sizeof(struct ums_softc)
};

MODULE_DEPEND(ums, usb, 1, 1, 1);

static int
ums_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usb_interface_descriptor_t *id;
	int size, ret;
	void *desc;
	usbd_status err;

	if (!uaa->iface)
		return (UMATCH_NONE);
	id = usbd_get_interface_descriptor(uaa->iface);
	if (!id || id->bInterfaceClass != UICLASS_HID)
		return (UMATCH_NONE);

	err = usbd_read_report_desc(uaa->iface, &desc, &size, M_TEMP);
	if (err)
		return (UMATCH_NONE);

	if (hid_is_collection(desc, size,
			      HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_MOUSE)))
		ret = UMATCH_IFACECLASS;
	else
		ret = UMATCH_NONE;

	kfree(desc, M_TEMP);
	return (ret);
}

static int
ums_attach(device_t self)
{
	struct ums_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usbd_interface_handle iface = uaa->iface;
	usb_endpoint_descriptor_t *ed;
	int size;
	void *desc;
	usbd_status err;
	u_int32_t flags;
	int i;
	struct hid_location loc_btn;

	sc->sc_disconnected = 1;
	sc->sc_iface = iface;
	sc->sc_dev = self;
	ed = usbd_interface2endpoint_descriptor(iface, 0);
	if (!ed) {
		kprintf("%s: could not read endpoint descriptor\n",
		       device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}

	DPRINTFN(10,("ums_attach: bLength=%d bDescriptorType=%d "
		     "bEndpointAddress=%d-%s bmAttributes=%d wMaxPacketSize=%d"
		     " bInterval=%d\n",
		     ed->bLength, ed->bDescriptorType,
		     UE_GET_ADDR(ed->bEndpointAddress),
		     UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN ? "in":"out",
		     UE_GET_XFERTYPE(ed->bmAttributes),
		     UGETW(ed->wMaxPacketSize), ed->bInterval));

	if (UE_GET_DIR(ed->bEndpointAddress) != UE_DIR_IN ||
	    UE_GET_XFERTYPE(ed->bmAttributes) != UE_INTERRUPT) {
		kprintf("%s: unexpected endpoint\n",
		       device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}

	err = usbd_read_report_desc(uaa->iface, &desc, &size, M_TEMP);
	if (err)
		return ENXIO;

	if (!hid_locate(desc, size, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X),
		       hid_input, &sc->sc_loc_x, &flags)) {
		kprintf("%s: mouse has no X report\n", device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}
	if ((flags & MOUSE_FLAGS_MASK) != MOUSE_FLAGS) {
		kprintf("%s: X report 0x%04x not supported\n",
		       device_get_nameunit(sc->sc_dev), flags);
		return ENXIO;
	}

	if (!hid_locate(desc, size, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y),
		       hid_input, &sc->sc_loc_y, &flags)) {
		kprintf("%s: mouse has no Y report\n", device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}
	if ((flags & MOUSE_FLAGS_MASK) != MOUSE_FLAGS) {
		kprintf("%s: Y report 0x%04x not supported\n",
		       device_get_nameunit(sc->sc_dev), flags);
		return ENXIO;
	}

	/* try to guess the Z activator: first check Z, then WHEEL */
	if (hid_locate(desc, size, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Z),
		       hid_input, &sc->sc_loc_z, &flags) ||
	    hid_locate(desc, size, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_WHEEL),
		       hid_input, &sc->sc_loc_z, &flags)) {
		if ((flags & MOUSE_FLAGS_MASK) != MOUSE_FLAGS) {
			sc->sc_loc_z.size = 0;	/* Bad Z coord, ignore it */
		} else {
			sc->flags |= UMS_Z;
		}
	}

	/* figure out the number of buttons */
	for (i = 1; i <= MAX_BUTTONS; i++)
		if (!hid_locate(desc, size, HID_USAGE2(HUP_BUTTON, i),
				hid_input, &loc_btn, 0))
			break;
	sc->nbuttons = i - 1;
	sc->sc_loc_btn = kmalloc(sizeof(struct hid_location)*sc->nbuttons,
				M_USBDEV, M_INTWAIT);

	kprintf("%s: %d buttons%s\n", device_get_nameunit(sc->sc_dev),
	       sc->nbuttons, sc->flags & UMS_Z? " and Z dir." : "");

	for (i = 1; i <= sc->nbuttons; i++)
		hid_locate(desc, size, HID_USAGE2(HUP_BUTTON, i),
				hid_input, &sc->sc_loc_btn[i-1], 0);

	sc->sc_isize = hid_report_size(desc, size, hid_input, &sc->sc_iid);
	sc->sc_ibuf = kmalloc(sc->sc_isize, M_USB, M_INTWAIT);
	sc->sc_ep_addr = ed->bEndpointAddress;
	sc->sc_disconnected = 0;
	kfree(desc, M_TEMP);

#ifdef USB_DEBUG
	DPRINTF(("ums_attach: sc=%p\n", sc));
	DPRINTF(("ums_attach: X\t%d/%d\n",
		 sc->sc_loc_x.pos, sc->sc_loc_x.size));
	DPRINTF(("ums_attach: Y\t%d/%d\n",
		 sc->sc_loc_y.pos, sc->sc_loc_y.size));
	if (sc->flags & UMS_Z)
		DPRINTF(("ums_attach: Z\t%d/%d\n",
			 sc->sc_loc_z.pos, sc->sc_loc_z.size));
	for (i = 1; i <= sc->nbuttons; i++) {
		DPRINTF(("ums_attach: B%d\t%d/%d\n",
			 i, sc->sc_loc_btn[i-1].pos,sc->sc_loc_btn[i-1].size));
	}
	DPRINTF(("ums_attach: size=%d, id=%d\n", sc->sc_isize, sc->sc_iid));
#endif

	if (sc->nbuttons > MOUSE_MSC_MAXBUTTON)
		sc->hw.buttons = MOUSE_MSC_MAXBUTTON;
	else
		sc->hw.buttons = sc->nbuttons;
	sc->hw.iftype = MOUSE_IF_USB;
	sc->hw.type = MOUSE_MOUSE;
	sc->hw.model = MOUSE_MODEL_GENERIC;
	sc->hw.hwid = 0;
	sc->mode.protocol = MOUSE_PROTO_MSC;
	sc->mode.rate = -1;
	sc->mode.resolution = MOUSE_RES_UNKNOWN;
	sc->mode.accelfactor = 0;
	sc->mode.level = 0;
	sc->mode.packetsize = MOUSE_MSC_PACKETSIZE;
	sc->mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
	sc->mode.syncmask[1] = MOUSE_MSC_SYNC;

	sc->status.flags = 0;
	sc->status.button = sc->status.obutton = 0;
	sc->status.dx = sc->status.dy = sc->status.dz = 0;

	sc->sc_cdev = make_dev(&ums_ops, device_get_unit(self),
		 	       UID_ROOT, GID_OPERATOR,
		 	       0644, "ums%d", device_get_unit(self));
	reference_dev(sc->sc_cdev);

	if (usbd_get_quirks(uaa->device)->uq_flags & UQ_SPUR_BUT_UP) {
		DPRINTF(("%s: Spurious button up events\n",
			device_get_nameunit(sc->sc_dev)));
		sc->flags |= UMS_SPUR_BUT_UP;
	}

	return 0;
}


static int
ums_detach(device_t self)
{
	struct ums_softc *sc = device_get_softc(self);

	if (sc->sc_enabled)
		ums_disable(sc);

	DPRINTF(("%s: disconnected\n", device_get_nameunit(self)));

	kfree(sc->sc_loc_btn, M_USB);
	kfree(sc->sc_ibuf, M_USB);

	/* someone waiting for data */
	/*
	 * XXX If we wakeup the process here, the device will be gone by
	 * the time the process gets a chance to notice. *_close and friends
	 * should be fixed to handle this case.
	 * Or we should do a delayed detach for this.
	 * Does this delay now force tsleep to exit with an error?
	 */
	if (sc->state & UMS_ASLEEP) {
		sc->state &= ~UMS_ASLEEP;
		wakeup(sc);
	}

	dev_ops_remove_minor(&ums_ops, /*-1, */device_get_unit(self));
	devfs_assume_knotes(sc->sc_cdev, &sc->rkq);
	release_dev(sc->sc_cdev);
        sc->sc_cdev = NULL;

	return 0;
}

void
ums_intr(usbd_xfer_handle xfer, usbd_private_handle addr,
	 usbd_status status)
{
	struct ums_softc *sc = addr;
	u_char *ibuf;
	int dx, dy, dz;
	u_char buttons = 0;
	int i;

#define UMS_BUT(i) ((i) < 3 ? (((i) + 2) % 3) : (i))

	DPRINTFN(5, ("ums_intr: sc=%p status=%d\n", sc, status));
	DPRINTFN(5, ("ums_intr: data = %02x %02x %02x\n",
		     sc->sc_ibuf[0], sc->sc_ibuf[1], sc->sc_ibuf[2]));

	if (status == USBD_CANCELLED)
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF(("ums_intr: status=%d\n", status));
		if (status == USBD_STALLED)
		    usbd_clear_endpoint_stall_async(sc->sc_intrpipe);
		return;
	}

	ibuf = sc->sc_ibuf;
	if (sc->sc_iid) {
		if (*ibuf++ != sc->sc_iid)
			return;
	}

	dx =  hid_get_data(ibuf, &sc->sc_loc_x);
	dy = -hid_get_data(ibuf, &sc->sc_loc_y);
	dz = -hid_get_data(ibuf, &sc->sc_loc_z);
	for (i = 0; i < sc->nbuttons; i++)
		if (hid_get_data(ibuf, &sc->sc_loc_btn[i]))
			buttons |= (1 << UMS_BUT(i));

	if (dx || dy || dz || (sc->flags & UMS_Z)
	    || buttons != sc->status.button) {
		DPRINTFN(5, ("ums_intr: x:%d y:%d z:%d buttons:0x%x\n",
			dx, dy, dz, buttons));

		sc->status.button = buttons;
		sc->status.dx += dx;
		sc->status.dy += dy;
		sc->status.dz += dz;

		/* Discard data in case of full buffer */
		if (sc->qcount == sizeof(sc->qbuf)) {
			DPRINTF(("Buffer full, discarded packet"));
			return;
		}

		/*
		 * The Qtronix keyboard has a built in PS/2 port for a mouse.
		 * The firmware once in a while posts a spurious button up
		 * event. This event we ignore by doing a timeout for 50 msecs.
		 * If we receive dx=dy=dz=buttons=0 before we add the event to
		 * the queue.
		 * In any other case we delete the timeout event.
		 */
		if (sc->flags & UMS_SPUR_BUT_UP &&
		    dx == 0 && dy == 0 && dz == 0 && buttons == 0) {
			callout_reset(&sc->sc_timeout, MS_TO_TICKS(50),
				    ums_add_to_queue_timeout, (void *) sc);
		} else {
			callout_stop(&sc->sc_timeout);
			ums_add_to_queue(sc, dx, dy, dz, buttons);
		}
	}
}

static void
ums_add_to_queue_timeout(void *priv)
{
	struct ums_softc *sc = priv;

	crit_enter();
	ums_add_to_queue(sc, 0, 0, 0, 0);
	crit_exit();
}

static void
ums_add_to_queue(struct ums_softc *sc, int dx, int dy, int dz, int buttons)
{
	/* Discard data in case of full buffer */
	if (sc->qhead+sc->mode.packetsize > sizeof(sc->qbuf)) {
		DPRINTF(("Buffer full, discarded packet"));
		return;
	}

	if (dx >  254)		dx =  254;
	if (dx < -256)		dx = -256;
	if (dy >  254)		dy =  254;
	if (dy < -256)		dy = -256;
	if (dz >  126)		dz =  126;
	if (dz < -128)		dz = -128;

	sc->qbuf[sc->qhead] = sc->mode.syncmask[1];
	sc->qbuf[sc->qhead] |= ~buttons & MOUSE_MSC_BUTTONS;
	sc->qbuf[sc->qhead+1] = dx >> 1;
	sc->qbuf[sc->qhead+2] = dy >> 1;
	sc->qbuf[sc->qhead+3] = dx - (dx >> 1);
	sc->qbuf[sc->qhead+4] = dy - (dy >> 1);

	if (sc->mode.level == 1) {
		sc->qbuf[sc->qhead+5] = dz >> 1;
		sc->qbuf[sc->qhead+6] = dz - (dz >> 1);
		sc->qbuf[sc->qhead+7] = ((~buttons >> 3)
					 & MOUSE_SYS_EXTBUTTONS);
	}

	sc->qhead += sc->mode.packetsize;
	sc->qcount += sc->mode.packetsize;
	/* wrap round at end of buffer */
	if (sc->qhead >= sizeof(sc->qbuf))
		sc->qhead = 0;

	/* someone waiting for data */
	if (sc->state & UMS_ASLEEP) {
		sc->state &= ~UMS_ASLEEP;
		wakeup(sc);
	}
	KNOTE(&sc->rkq.ki_note, 0);
}

static int
ums_enable(void *v)
{
	struct ums_softc *sc = v;

	usbd_status err;

	if (sc->sc_enabled)
		return EBUSY;

	sc->sc_enabled = 1;
	sc->qcount = 0;
	sc->qhead = sc->qtail = 0;
	sc->status.flags = 0;
	sc->status.button = sc->status.obutton = 0;
	sc->status.dx = sc->status.dy = sc->status.dz = 0;

	callout_init(&sc->sc_timeout);

	/* Set up interrupt pipe. */
	err = usbd_open_pipe_intr(sc->sc_iface, sc->sc_ep_addr,
				USBD_SHORT_XFER_OK, &sc->sc_intrpipe, sc,
				sc->sc_ibuf, sc->sc_isize, ums_intr,
				USBD_DEFAULT_INTERVAL);
	if (err) {
		DPRINTF(("ums_enable: usbd_open_pipe_intr failed, error=%d\n",
			 err));
		sc->sc_enabled = 0;
		return (EIO);
	}
	return (0);
}

static void
ums_disable(void *priv)
{
	struct ums_softc *sc = priv;

	callout_stop(&sc->sc_timeout);

	/* Disable interrupts. */
	usbd_abort_pipe(sc->sc_intrpipe);
	usbd_close_pipe(sc->sc_intrpipe);

	sc->sc_enabled = 0;

	if (sc->qcount != 0)
		DPRINTF(("Discarded %d bytes in queue\n", sc->qcount));
}

static int
ums_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ums_softc *sc;

	sc = devclass_get_softc(ums_devclass, UMSUNIT(dev));
	if (sc == NULL)
		return (ENXIO);

	return ums_enable(sc);
}

static int
ums_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ums_softc *sc;

	sc = devclass_get_softc(ums_devclass, UMSUNIT(dev));

	if (!sc)
		return 0;

	if (sc->sc_enabled)
		ums_disable(sc);

	return 0;
}

static int
ums_read(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct ums_softc *sc;
	char buf[sizeof(sc->qbuf)];
	int l = 0;
	int error;

	sc = devclass_get_softc(ums_devclass, UMSUNIT(dev));

	crit_enter();
	if (!sc) {
		crit_exit();
		return EIO;
	}

	while (sc->qcount == 0 )  {
		if (ap->a_ioflag & IO_NDELAY) {		/* non-blocking I/O */
			crit_exit();
			return EWOULDBLOCK;
		}

		sc->state |= UMS_ASLEEP;	/* blocking I/O */
		error = tsleep(sc, PCATCH, "umsrea", 0);
		if (error) {
			crit_exit();
			return error;
		} else if (!sc->sc_enabled) {
			crit_exit();
			return EINTR;
		}
		/* check whether the device is still there */

		sc = devclass_get_softc(ums_devclass, UMSUNIT(dev));
		if (!sc) {
			crit_exit();
			return EIO;
		}
	}

	/*
	 * The writer process only extends qcount and qtail. We could copy
	 * them and use the copies to do the copying out of the queue.
	 */

	while ((sc->qcount > 0) && (uio->uio_resid > 0)) {
		l = (sc->qcount < uio->uio_resid? sc->qcount:uio->uio_resid);
		if (l > sizeof(buf))
			l = sizeof(buf);
		if (l > sizeof(sc->qbuf) - sc->qtail)		/* transfer till end of buf */
			l = sizeof(sc->qbuf) - sc->qtail;

		crit_exit();
		uiomove(&sc->qbuf[sc->qtail], l, uio);
		crit_enter();

		if ( sc->qcount - l < 0 ) {
			DPRINTF(("qcount below 0, count=%d l=%d\n", sc->qcount, l));
			sc->qcount = l;
		}
		sc->qcount -= l;	/* remove the bytes from the buffer */
		sc->qtail = (sc->qtail + l) % sizeof(sc->qbuf);
	}
	crit_exit();

	return 0;
}

static struct filterops ums_filtops =
	{ FILTEROP_ISFD, NULL, ums_filt_detach, ums_filt };

static int
ums_kqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct ums_softc *sc;
	struct klist *klist;

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		sc = devclass_get_softc(ums_devclass, UMSUNIT(dev));
		kn->kn_fop = &ums_filtops;
		kn->kn_hook = (caddr_t)sc;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	klist = &sc->rkq.ki_note;
	knote_insert(klist, kn);

	return (0);
}

static void
ums_filt_detach(struct knote *kn)
{
	struct ums_softc *sc = (struct ums_softc *)kn->kn_hook;
	struct klist *klist;

	klist = &sc->rkq.ki_note;
	knote_remove(klist, kn);
}

static int
ums_filt(struct knote *kn, long hint)
{
	struct ums_softc *sc = (struct ums_softc *)kn->kn_hook;
	int ready = 0;

	crit_enter();
	if (sc->qcount)
		ready = 1;
	crit_exit();

	return (ready);
}

int
ums_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ums_softc *sc;
	int error = 0;
	mousemode_t mode;

	sc = devclass_get_softc(ums_devclass, UMSUNIT(dev));

	if (!sc)
		return EIO;

	switch(ap->a_cmd) {
	case MOUSE_GETHWINFO:
		*(mousehw_t *)ap->a_data = sc->hw;
		break;
	case MOUSE_GETMODE:
		*(mousemode_t *)ap->a_data = sc->mode;
		break;
	case MOUSE_SETMODE:
		mode = *(mousemode_t *)ap->a_data;

		if (mode.level == -1)
			/* don't change the current setting */
			;
		else if ((mode.level < 0) || (mode.level > 1))
			return (EINVAL);

		crit_enter();
		sc->mode.level = mode.level;

		if (sc->mode.level == 0) {
			if (sc->nbuttons > MOUSE_MSC_MAXBUTTON)
				sc->hw.buttons = MOUSE_MSC_MAXBUTTON;
			else
				sc->hw.buttons = sc->nbuttons;
			sc->mode.protocol = MOUSE_PROTO_MSC;
			sc->mode.packetsize = MOUSE_MSC_PACKETSIZE;
			sc->mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
			sc->mode.syncmask[1] = MOUSE_MSC_SYNC;
		} else if (sc->mode.level == 1) {
			if (sc->nbuttons > MOUSE_SYS_MAXBUTTON)
				sc->hw.buttons = MOUSE_SYS_MAXBUTTON;
			else
				sc->hw.buttons = sc->nbuttons;
			sc->mode.protocol = MOUSE_PROTO_SYSMOUSE;
			sc->mode.packetsize = MOUSE_SYS_PACKETSIZE;
			sc->mode.syncmask[0] = MOUSE_SYS_SYNCMASK;
			sc->mode.syncmask[1] = MOUSE_SYS_SYNC;
		}

		bzero(sc->qbuf, sizeof(sc->qbuf));
		sc->qhead = sc->qtail = sc->qcount = 0;
		crit_exit();

		break;
	case MOUSE_GETLEVEL:
		*(int *)ap->a_data = sc->mode.level;
		break;
	case MOUSE_SETLEVEL:
		if (*(int *)ap->a_data < 0 || *(int *)ap->a_data > 1)
			return (EINVAL);

		crit_enter();
		sc->mode.level = *(int *)ap->a_data;

		if (sc->mode.level == 0) {
			if (sc->nbuttons > MOUSE_MSC_MAXBUTTON)
				sc->hw.buttons = MOUSE_MSC_MAXBUTTON;
			else
				sc->hw.buttons = sc->nbuttons;
			sc->mode.protocol = MOUSE_PROTO_MSC;
			sc->mode.packetsize = MOUSE_MSC_PACKETSIZE;
			sc->mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
			sc->mode.syncmask[1] = MOUSE_MSC_SYNC;
		} else if (sc->mode.level == 1) {
			if (sc->nbuttons > MOUSE_SYS_MAXBUTTON)
				sc->hw.buttons = MOUSE_SYS_MAXBUTTON;
			else
				sc->hw.buttons = sc->nbuttons;
			sc->mode.protocol = MOUSE_PROTO_SYSMOUSE;
			sc->mode.packetsize = MOUSE_SYS_PACKETSIZE;
			sc->mode.syncmask[0] = MOUSE_SYS_SYNCMASK;
			sc->mode.syncmask[1] = MOUSE_SYS_SYNC;
		}

		bzero(sc->qbuf, sizeof(sc->qbuf));
		sc->qhead = sc->qtail = sc->qcount = 0;
		crit_exit();

		break;
	case MOUSE_GETSTATUS: {
		mousestatus_t *status = (mousestatus_t *) ap->a_data;

		crit_enter();
		*status = sc->status;
		sc->status.obutton = sc->status.button;
		sc->status.button = 0;
		sc->status.dx = sc->status.dy = sc->status.dz = 0;
		crit_exit();

		if (status->dx || status->dy || status->dz)
			status->flags |= MOUSE_POSCHANGED;
		if (status->button != status->obutton)
			status->flags |= MOUSE_BUTTONSCHANGED;
		break;
		}
	default:
		error = ENOTTY;
	}

	return error;
}

DRIVER_MODULE(ums, uhub, ums_driver, ums_devclass, usbd_driver_load, 0);
