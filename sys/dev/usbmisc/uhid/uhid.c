/*
 * $NetBSD: uhid.c,v 1.46 2001/11/13 06:24:55 lukem Exp $
 * $FreeBSD: src/sys/dev/usb/uhid.c,v 1.65 2003/11/09 09:17:22 tanimura Exp $
 */

/* Also already merged from NetBSD:
 *	$NetBSD: uhid.c,v 1.54 2002/09/23 05:51:21 simonb Exp $
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
 * HID spec: http://www.usb.org/developers/data/usbhid10.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>
#include <sys/filio.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/event.h>
#include <sys/sysctl.h>
#include <sys/thread2.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbhid.h>

#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>
#include <bus/usb/hid.h>

/* Report descriptor for broken Wacom Graphire */
#include <bus/usb/ugraphire_rdesc.h>

#ifdef USB_DEBUG
#define DPRINTF(x)	if (uhiddebug) kprintf x
#define DPRINTFN(n,x)	if (uhiddebug>(n)) kprintf x
int	uhiddebug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, uhid, CTLFLAG_RW, 0, "USB uhid");
SYSCTL_INT(_hw_usb_uhid, OID_AUTO, debug, CTLFLAG_RW,
	   &uhiddebug, 0, "uhid debug level");
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

struct uhid_softc {
	device_t sc_dev;			/* base device */
	usbd_device_handle sc_udev;
	usbd_interface_handle sc_iface;	/* interface */
	usbd_pipe_handle sc_intrpipe;	/* interrupt pipe */
	int sc_ep_addr;

	int sc_isize;
	int sc_osize;
	int sc_fsize;
	u_int8_t sc_iid;
	u_int8_t sc_oid;
	u_int8_t sc_fid;

	u_char *sc_ibuf;
	u_char *sc_obuf;

	void *sc_repdesc;
	int sc_repdesc_size;

	struct clist sc_q;
	struct kqinfo sc_rkq;
	struct proc *sc_async;	/* process that wants SIGIO */
	u_char sc_state;	/* driver state */
#define	UHID_OPEN	0x01	/* device is open */
#define	UHID_ASLP	0x02	/* waiting for device data */
#define UHID_NEEDCLEAR	0x04	/* needs clearing endpoint stall */
#define UHID_IMMED	0x08	/* return read data immediately */

	int sc_refcnt;
	u_char sc_dying;
};

#define	UHIDUNIT(dev)	(minor(dev))
#define	UHID_CHUNK	128	/* chunk size for read */
#define	UHID_BSIZE	1020	/* buffer size */

d_open_t	uhidopen;
d_close_t	uhidclose;
d_read_t	uhidread;
d_write_t	uhidwrite;
d_ioctl_t	uhidioctl;
d_kqfilter_t	uhidkqfilter;

static void uhidfilt_detach(struct knote *);
static int uhidfilt_read(struct knote *, long);
static int uhidfilt_write(struct knote *, long);

static struct dev_ops uhid_ops = {
	{ "uhid", 0, 0 },
	.d_open =	uhidopen,
	.d_close =	uhidclose,
	.d_read =	uhidread,
	.d_write =	uhidwrite,
	.d_ioctl =	uhidioctl,
	.d_kqfilter =	uhidkqfilter
};

static void uhid_intr(usbd_xfer_handle, usbd_private_handle,
			   usbd_status);

static int uhid_do_read(struct uhid_softc *, struct uio *uio, int);
static int uhid_do_write(struct uhid_softc *, struct uio *uio, int);
static int uhid_do_ioctl(struct uhid_softc *, u_long, caddr_t, int);

static device_probe_t uhid_match;
static device_attach_t uhid_attach;
static device_detach_t uhid_detach;

static devclass_t uhid_devclass;

static kobj_method_t uhid_methods[] = {
	DEVMETHOD(device_probe, uhid_match),
	DEVMETHOD(device_attach, uhid_attach),
	DEVMETHOD(device_detach, uhid_detach),
	DEVMETHOD_END
};

static driver_t uhid_driver = {
	"uhid",
	uhid_methods,
	sizeof(struct uhid_softc)
};

MODULE_DEPEND(uhid, usb, 1, 1, 1);

static int
uhid_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usb_interface_descriptor_t *id;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);
	id = usbd_get_interface_descriptor(uaa->iface);
	if (id == NULL || id->bInterfaceClass != UICLASS_HID)
		return (UMATCH_NONE);
	if (uaa->matchlvl)
		return (uaa->matchlvl);
	return (UMATCH_IFACECLASS_GENERIC);
}

static int
uhid_attach(device_t self)
{
	struct uhid_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usbd_interface_handle iface = uaa->iface;
	usb_endpoint_descriptor_t *ed;
	int size;
	void *desc;
	usbd_status err;

	sc->sc_udev = uaa->device;
	sc->sc_iface = iface;
	sc->sc_dev = self;

	ed = usbd_interface2endpoint_descriptor(iface, 0);
	if (ed == NULL) {
		kprintf("%s: could not read endpoint descriptor\n",
		       device_get_nameunit(sc->sc_dev));
		sc->sc_dying = 1;
		return ENXIO;
	}

	DPRINTFN(10,("uhid_attach: bLength=%d bDescriptorType=%d "
		     "bEndpointAddress=%d-%s bmAttributes=%d wMaxPacketSize=%d"
		     " bInterval=%d\n",
		     ed->bLength, ed->bDescriptorType,
		     ed->bEndpointAddress & UE_ADDR,
		     UE_GET_DIR(ed->bEndpointAddress)==UE_DIR_IN? "in" : "out",
		     ed->bmAttributes & UE_XFERTYPE,
		     UGETW(ed->wMaxPacketSize), ed->bInterval));

	if (UE_GET_DIR(ed->bEndpointAddress) != UE_DIR_IN ||
	    (ed->bmAttributes & UE_XFERTYPE) != UE_INTERRUPT) {
		kprintf("%s: unexpected endpoint\n", device_get_nameunit(sc->sc_dev));
		sc->sc_dying = 1;
		return ENXIO;
	}

	sc->sc_ep_addr = ed->bEndpointAddress;

	/* The report descriptor for the Wacom Graphire is broken. */
	if (uaa->vendor == 0x056a && uaa->product == 0x0010 /* &&
	    uaa->revision == 0x???? */) { /* XXX should use revision */
		size = sizeof uhid_graphire_report_descr;
		desc = kmalloc(size, M_USBDEV, M_INTWAIT);
		err = USBD_NORMAL_COMPLETION;
		memcpy(desc, uhid_graphire_report_descr, size);
	} else {
		desc = NULL;
		err = usbd_read_report_desc(uaa->iface, &desc, &size,M_USBDEV);
	}

	if (err) {
		kprintf("%s: no report descriptor\n", device_get_nameunit(sc->sc_dev));
		sc->sc_dying = 1;
		return ENXIO;
	}

	(void)usbd_set_idle(iface, 0, 0);

	sc->sc_isize = hid_report_size(desc, size, hid_input,   &sc->sc_iid);
	sc->sc_osize = hid_report_size(desc, size, hid_output,  &sc->sc_oid);
	sc->sc_fsize = hid_report_size(desc, size, hid_feature, &sc->sc_fid);

	sc->sc_repdesc = desc;
	sc->sc_repdesc_size = size;

	make_dev(&uhid_ops, device_get_unit(self),
		 UID_ROOT, GID_OPERATOR,
		 0644, "uhid%d", device_get_unit(self));

	return 0;
}

static int
uhid_detach(device_t self)
{
	struct uhid_softc *sc = device_get_softc(self);

	DPRINTF(("uhid_detach: sc=%p\n", sc));

	sc->sc_dying = 1;
	if (sc->sc_intrpipe != NULL)
		usbd_abort_pipe(sc->sc_intrpipe);

	if (sc->sc_state & UHID_OPEN) {
		crit_enter();
		if (--sc->sc_refcnt >= 0) {
			/* Wake everyone */
			wakeup(&sc->sc_q);
			/* Wait for processes to go away. */
			usb_detach_wait(sc->sc_dev);
		}
		crit_exit();
	}

	dev_ops_remove_minor(&uhid_ops, device_get_unit(self));

	if (sc->sc_repdesc)
		kfree(sc->sc_repdesc, M_USBDEV);

	return (0);
}

void
uhid_intr(usbd_xfer_handle xfer, usbd_private_handle addr, usbd_status status)
{
	struct uhid_softc *sc = addr;

#ifdef USB_DEBUG
	if (uhiddebug > 5) {
		u_int32_t cc, i;

		usbd_get_xfer_status(xfer, NULL, NULL, &cc, NULL);
		DPRINTF(("uhid_intr: status=%d cc=%d\n", status, cc));
		DPRINTF(("uhid_intr: data ="));
		for (i = 0; i < cc; i++)
			DPRINTF((" %02x", sc->sc_ibuf[i]));
		DPRINTF(("\n"));
	}
#endif

	if (status == USBD_CANCELLED)
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF(("uhid_intr: status=%d\n", status));
		if (status == USBD_STALLED)
		    sc->sc_state |= UHID_NEEDCLEAR;
		return;
	}

	(void) b_to_q(sc->sc_ibuf, sc->sc_isize, &sc->sc_q);

	if (sc->sc_state & UHID_ASLP) {
		sc->sc_state &= ~UHID_ASLP;
		DPRINTFN(5, ("uhid_intr: waking %p\n", &sc->sc_q));
		wakeup(&sc->sc_q);
	}
	KNOTE(&sc->sc_rkq.ki_note, 0);
	if (sc->sc_async != NULL) {
		DPRINTFN(3, ("uhid_intr: sending SIGIO %p\n", sc->sc_async));
		ksignal(sc->sc_async, SIGIO);
	}
}

int
uhidopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uhid_softc *sc;
	usbd_status err;

	sc = devclass_get_softc(uhid_devclass, UHIDUNIT(dev));
	if (sc == NULL)
		return (ENXIO);

	DPRINTF(("uhidopen: sc=%p\n", sc));

	if (sc->sc_dying)
		return (ENXIO);

	if (sc->sc_state & UHID_OPEN)
		return (EBUSY);
	sc->sc_state |= UHID_OPEN;

	if ((clist_alloc_cblocks(&sc->sc_q, UHID_BSIZE,
				 UHID_BSIZE), 0) == -1) {
		sc->sc_state &= ~UHID_OPEN;
		return (ENOMEM);
	}

	sc->sc_ibuf = kmalloc(sc->sc_isize, M_USBDEV, M_WAITOK);
	sc->sc_obuf = kmalloc(sc->sc_osize, M_USBDEV, M_WAITOK);

	/* Set up interrupt pipe. */
	err = usbd_open_pipe_intr(sc->sc_iface, sc->sc_ep_addr,
		  USBD_SHORT_XFER_OK, &sc->sc_intrpipe, sc, sc->sc_ibuf,
		  sc->sc_isize, uhid_intr, USBD_DEFAULT_INTERVAL);
	if (err) {
		DPRINTF(("uhidopen: usbd_open_pipe_intr failed, "
			 "error=%d\n",err));
		kfree(sc->sc_ibuf, M_USBDEV);
		kfree(sc->sc_obuf, M_USBDEV);
		sc->sc_ibuf = sc->sc_obuf = NULL;

		sc->sc_state &= ~UHID_OPEN;
		return (EIO);
	}

	sc->sc_state &= ~UHID_IMMED;

	sc->sc_async = NULL;

	return (0);
}

int
uhidclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uhid_softc *sc;

	sc = devclass_get_softc(uhid_devclass, UHIDUNIT(dev));

	DPRINTF(("uhidclose: sc=%p\n", sc));

	/* Disable interrupts. */
	usbd_abort_pipe(sc->sc_intrpipe);
	usbd_close_pipe(sc->sc_intrpipe);
	sc->sc_intrpipe = 0;

	ndflush(&sc->sc_q, sc->sc_q.c_cc);
	clist_free_cblocks(&sc->sc_q);

	kfree(sc->sc_ibuf, M_USBDEV);
	kfree(sc->sc_obuf, M_USBDEV);
	sc->sc_ibuf = sc->sc_obuf = NULL;

	sc->sc_state &= ~UHID_OPEN;

	sc->sc_async = NULL;

	return (0);
}

int
uhid_do_read(struct uhid_softc *sc, struct uio *uio, int flag)
{
	int error = 0;
	size_t length;
	u_char buffer[UHID_CHUNK];
	usbd_status err;

	DPRINTFN(1, ("uhidread\n"));
	if (sc->sc_state & UHID_IMMED) {
		DPRINTFN(1, ("uhidread immed\n"));

		err = usbd_get_report(sc->sc_iface, UHID_INPUT_REPORT,
			  sc->sc_iid, buffer, sc->sc_isize);
		if (err)
			return (EIO);
		return (uiomove(buffer, sc->sc_isize, uio));
	}

	crit_enter();
	while (sc->sc_q.c_cc == 0) {
		if (flag & IO_NDELAY) {
			crit_exit();
			return (EWOULDBLOCK);
		}
		sc->sc_state |= UHID_ASLP;
		DPRINTFN(5, ("uhidread: sleep on %p\n", &sc->sc_q));
		error = tsleep(&sc->sc_q, PCATCH, "uhidrea", 0);
		DPRINTFN(5, ("uhidread: woke, error=%d\n", error));
		if (sc->sc_dying)
			error = EIO;
		if (error) {
			sc->sc_state &= ~UHID_ASLP;
			break;
		}
		if (sc->sc_state & UHID_NEEDCLEAR) {
			DPRINTFN(-1,("uhidread: clearing stall\n"));
			sc->sc_state &= ~UHID_NEEDCLEAR;
			usbd_clear_endpoint_stall(sc->sc_intrpipe);
		}
	}
	crit_exit();

	/* Transfer as many chunks as possible. */
	while (sc->sc_q.c_cc > 0 && uio->uio_resid > 0 && !error) {
		length = szmin(sc->sc_q.c_cc, uio->uio_resid);
		if (length > sizeof(buffer))
			length = sizeof(buffer);

		/* Remove a small chunk from the input queue. */
		(void) q_to_b(&sc->sc_q, buffer, length);
		DPRINTFN(5, ("uhidread: got %lu chars\n", (u_long)length));

		/* Copy the data to the user process. */
		if ((error = uiomove(buffer, length, uio)) != 0)
			break;
	}

	return (error);
}

int
uhidread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uhid_softc *sc;
	int error;

	sc = devclass_get_softc(uhid_devclass, UHIDUNIT(dev));

	sc->sc_refcnt++;
	error = uhid_do_read(sc, ap->a_uio, ap->a_ioflag);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(sc->sc_dev);
	return (error);
}

int
uhid_do_write(struct uhid_softc *sc, struct uio *uio, int flag)
{
	int error;
	int size;
	usbd_status err;

	DPRINTFN(1, ("uhidwrite\n"));

	if (sc->sc_dying)
		return (EIO);

	size = sc->sc_osize;
	if (uio->uio_resid != size)
		return (EINVAL);
	error = uiomove(sc->sc_obuf, size, uio);
	if (!error) {
		if (sc->sc_oid)
			err = usbd_set_report(sc->sc_iface, UHID_OUTPUT_REPORT,
				  sc->sc_obuf[0], sc->sc_obuf+1, size-1);
		else
			err = usbd_set_report(sc->sc_iface, UHID_OUTPUT_REPORT,
				  0, sc->sc_obuf, size);
		if (err)
			error = EIO;
	}

	return (error);
}

int
uhidwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uhid_softc *sc;
	int error;

	sc = devclass_get_softc(uhid_devclass, UHIDUNIT(dev));

	sc->sc_refcnt++;
	error = uhid_do_write(sc, ap->a_uio, ap->a_ioflag);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(sc->sc_dev);
	return (error);
}

int
uhid_do_ioctl(struct uhid_softc *sc, u_long cmd, caddr_t addr, int flag)
{
	struct usb_ctl_report_desc *rd;
	struct usb_ctl_report *re;
	int size, id;
	usbd_status err;

	DPRINTFN(2, ("uhidioctl: cmd=%lx\n", cmd));

	if (sc->sc_dying)
		return (EIO);

	switch (cmd) {
	case FIOASYNC:
		if (*(int *)addr) {
			if (sc->sc_async != NULL)
				return (EBUSY);
			sc->sc_async = curproc;
			DPRINTF(("uhid_do_ioctl: FIOASYNC %p\n", sc->sc_async));
		} else
			sc->sc_async = NULL;
		break;

	/* XXX this is not the most general solution. */
	case TIOCSPGRP:
		if (sc->sc_async == NULL)
			return (EINVAL);
		if (*(int *)addr != sc->sc_async->p_pgid)
			return (EPERM);
		break;

	case USB_GET_REPORT_DESC:
		rd = (struct usb_ctl_report_desc *)addr;
		size = min(sc->sc_repdesc_size, sizeof rd->ucrd_data);
		rd->ucrd_size = size;
		memcpy(rd->ucrd_data, sc->sc_repdesc, size);
		break;

	case USB_SET_IMMED:
		if (*(int *)addr) {
			/* XXX should read into ibuf, but does it matter? */
			err = usbd_get_report(sc->sc_iface, UHID_INPUT_REPORT,
				  sc->sc_iid, sc->sc_ibuf, sc->sc_isize);
			if (err)
				return (EOPNOTSUPP);

			sc->sc_state |=  UHID_IMMED;
		} else
			sc->sc_state &= ~UHID_IMMED;
		break;

	case USB_GET_REPORT:
		re = (struct usb_ctl_report *)addr;
		switch (re->ucr_report) {
		case UHID_INPUT_REPORT:
			size = sc->sc_isize;
			id = sc->sc_iid;
			break;
		case UHID_OUTPUT_REPORT:
			size = sc->sc_osize;
			id = sc->sc_oid;
			break;
		case UHID_FEATURE_REPORT:
			size = sc->sc_fsize;
			id = sc->sc_fid;
			break;
		default:
			return (EINVAL);
		}
		err = usbd_get_report(sc->sc_iface, re->ucr_report, id, re->ucr_data,
			  size);
		if (err)
			return (EIO);
		break;

	case USB_SET_REPORT:
		re = (struct usb_ctl_report *)addr;
		switch (re->ucr_report) {
		case UHID_INPUT_REPORT:
			size = sc->sc_isize;
			id = sc->sc_iid;
			break;
		case UHID_OUTPUT_REPORT:
			size = sc->sc_osize;
			id = sc->sc_oid;
			break;
		case UHID_FEATURE_REPORT:
			size = sc->sc_fsize;
			id = sc->sc_fid;
			break;
		default:
			return (EINVAL);
		}
		err = usbd_set_report(sc->sc_iface, re->ucr_report, id, re->ucr_data,
			  size);
		if (err)
			return (EIO);
		break;

	case USB_GET_REPORT_ID:
		*(int *)addr = 0;	/* XXX: we only support reportid 0? */
		break;

	default:
		return (EINVAL);
	}
	return (0);
}

int
uhidioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uhid_softc *sc;
	int error;

	sc = devclass_get_softc(uhid_devclass, UHIDUNIT(dev));

	sc->sc_refcnt++;
	error = uhid_do_ioctl(sc, ap->a_cmd, ap->a_data, ap->a_fflag);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(sc->sc_dev);
	return (error);
}

static struct filterops uhidfiltops_read =
	{ FILTEROP_ISFD, NULL, uhidfilt_detach, uhidfilt_read };
static struct filterops uhidfiltops_write =
	{ FILTEROP_ISFD, NULL, uhidfilt_detach, uhidfilt_write };

int
uhidkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct uhid_softc *sc;
	struct klist *klist;

	sc = devclass_get_softc(uhid_devclass, UHIDUNIT(dev));

	if (sc->sc_dying) {
		ap->a_result = 1;
		return (0);
	}

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &uhidfiltops_read;
		kn->kn_hook = (caddr_t)sc;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &uhidfiltops_write;
		kn->kn_hook = (caddr_t)sc;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	klist = &sc->sc_rkq.ki_note;
	knote_insert(klist, kn);

	return (0);
}

static void
uhidfilt_detach(struct knote *kn)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
	struct uhid_softc *sc;
	struct klist *klist;

	sc = devclass_get_softc(uhid_devclass, UHIDUNIT(dev));

	klist = &sc->sc_rkq.ki_note;
	knote_remove(klist, kn);
}

static int
uhidfilt_read(struct knote *kn, long hint)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
	struct uhid_softc *sc;
	int ready = 0;

	sc = devclass_get_softc(uhid_devclass, UHIDUNIT(dev));

	crit_enter();
	if (sc->sc_q.c_cc > 0)
		ready = 1;
	crit_exit();

	return (ready);
}

static int
uhidfilt_write(struct knote *kn, long hint)
{
	return (1);
}

DRIVER_MODULE(uhid, uhub, uhid_driver, uhid_devclass, usbd_driver_load, NULL);
