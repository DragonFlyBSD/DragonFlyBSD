/*
 * $NetBSD: ugen.c,v 1.27 1999/10/28 12:08:38 augustss Exp $
 * $NetBSD: ugen.c,v 1.59 2002/07/11 21:14:28 augustss Exp $
 * $FreeBSD: src/sys/dev/usb/ugen.c,v 1.81 2003/11/09 09:17:22 tanimura Exp $
 */

/* 
 * Also already merged from NetBSD:
 *	$NetBSD: ugen.c,v 1.61 2002/09/23 05:51:20 simonb Exp $
 *	$NetBSD: ugen.c,v 1.64 2003/06/28 14:21:46 darrenr Exp $
 *	$NetBSD: ugen.c,v 1.65 2003/06/29 22:30:56 fvdl Exp $
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


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <sys/vnode.h>
#include <sys/event.h>
#include <sys/sysctl.h>
#include <sys/thread2.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>

#include "ugenbuf.h"

SYSCTL_NODE(_hw_usb, OID_AUTO, ugen, CTLFLAG_RW, 0, "USB ugen");

#ifdef USB_DEBUG
#define DPRINTF(x)	if (ugendebug) kprintf x
#define DPRINTFN(n,x)	if (ugendebug>(n)) kprintf x
int	ugendebug = 0;
SYSCTL_INT(_hw_usb_ugen, OID_AUTO, debug, CTLFLAG_RW,
	   &ugendebug, 0, "ugen debug level");
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

static int ugen_bufsize = 16384;
SYSCTL_INT(_hw_usb_ugen, OID_AUTO, bufsize, CTLFLAG_RW,
	   &ugen_bufsize, 0, "ugen temporary buffer size");

#define	UGEN_CHUNK	128	/* chunk size for read */
#define	UGEN_IBSIZE	1020	/* buffer size */

#define	UGEN_NISOFRAMES	500	/* 0.5 seconds worth */
#define UGEN_NISOREQS	6	/* number of outstanding xfer requests */
#define UGEN_NISORFRMS	4	/* number of frames (miliseconds) per req */

struct ugen_endpoint {
	struct ugen_softc *sc;
	cdev_t dev;
	usb_endpoint_descriptor_t *edesc;
	usbd_interface_handle iface;
	int state;
#define	UGEN_ASLP	0x02	/* waiting for data */
#define UGEN_SHORT_OK	0x04	/* short xfers are OK */
	usbd_pipe_handle pipeh;
	struct clist q;
	struct kqinfo rkq;
	u_char *ibuf;		/* start of buffer (circular for isoc) */
	u_char *fill;		/* location for input (isoc) */
	u_char *limit;		/* end of circular buffer (isoc) */
	u_char *cur;		/* current read location (isoc) */
	u_int32_t timeout;
	struct isoreq {
		struct ugen_endpoint *sce;
		usbd_xfer_handle xfer;
		void *dmabuf;
		u_int16_t sizes[UGEN_NISORFRMS];
	} isoreqs[UGEN_NISOREQS];
};

struct ugen_softc {
	device_t sc_dev;		/* base device */
	usbd_device_handle sc_udev;

	char sc_is_open[USB_MAX_ENDPOINTS];
	struct ugen_endpoint sc_endpoints[USB_MAX_ENDPOINTS][2];
#define OUT 0
#define IN  1

	int sc_refcnt;
	u_char sc_dying;
};

d_open_t  ugenopen;
d_close_t ugenclose;
d_read_t  ugenread;
d_write_t ugenwrite;
d_ioctl_t ugenioctl;
d_kqfilter_t ugenkqfilter;

static void ugen_filt_detach(struct knote *);
static int ugen_filt_read(struct knote *, long);
static int ugen_filt_write(struct knote *, long);

static struct dev_ops ugen_ops = {
	{ "ugen", 0, 0 },
	.d_open =	ugenopen,
	.d_close =	ugenclose,
	.d_read =	ugenread,
	.d_write =	ugenwrite,
	.d_ioctl =	ugenioctl,
	.d_kqfilter = 	ugenkqfilter
};

static void ugenintr(usbd_xfer_handle xfer, usbd_private_handle addr,
			    usbd_status status);
static void ugen_isoc_rintr(usbd_xfer_handle xfer, usbd_private_handle addr,
			    usbd_status status);
static int ugen_do_read(struct ugen_softc *, int, struct uio *, int);
static int ugen_do_write(struct ugen_softc *, int, struct uio *, int);
static int ugen_do_ioctl(struct ugen_softc *, int, u_long,
			    caddr_t, int);
static void ugen_make_devnodes(struct ugen_softc *sc);
static void ugen_destroy_devnodes(struct ugen_softc *sc);
static int ugen_set_config(struct ugen_softc *sc, int configno);
static usb_config_descriptor_t *ugen_get_cdesc(struct ugen_softc *sc,
						int index, int *lenp);
static usbd_status ugen_set_interface(struct ugen_softc *, int, int);
static int ugen_get_alt_index(struct ugen_softc *sc, int ifaceidx);

#define UGENUNIT(n) ((lminor(n) >> 4) & 0xff)
#define UGENENDPOINT(n) (minor(n) & 0xf)
#define UGENMINOR(u, e) (((u & 0xf) << 4) | ((u & 0xf0) << 12) | (e))
#define UGENUNITMASK	0xffff00f0

static device_probe_t ugen_match;
static device_attach_t ugen_attach;
static device_detach_t ugen_detach;

static devclass_t ugen_devclass;

static kobj_method_t ugen_methods[] = {
	DEVMETHOD(device_probe, ugen_match),
	DEVMETHOD(device_attach, ugen_attach),
	DEVMETHOD(device_detach, ugen_detach),
	DEVMETHOD_END
};

static driver_t ugen_driver = {
	"ugen",
	ugen_methods,
	sizeof(struct ugen_softc)
};

MODULE_DEPEND(ugen, usb, 1, 1, 1);

static int
ugen_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

#if 0
	if (uaa->matchlvl)
		return (uaa->matchlvl);
#endif
	if (uaa->usegeneric)
		return (UMATCH_GENERIC);
	else
		return (UMATCH_NONE);
}

static int
ugen_attach(device_t self)
{
	struct ugen_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usbd_device_handle udev;
	usbd_status err;
	int conf;

	sc->sc_dev = self;
	sc->sc_udev = udev = uaa->device;

	memset(sc->sc_endpoints, 0, sizeof sc->sc_endpoints);

	/* First set configuration index 0, the default one for ugen. */
	err = usbd_set_config_index(udev, 0, 0);
	if (err) {
		kprintf("%s: setting configuration index 0 failed\n",
		       device_get_nameunit(sc->sc_dev));
		sc->sc_dying = 1;
		return ENXIO;
	}
	conf = usbd_get_config_descriptor(udev)->bConfigurationValue;

	/* Set up all the local state for this configuration. */
	err = ugen_set_config(sc, conf);
	if (err) {
		kprintf("%s: setting configuration %d failed\n",
		       device_get_nameunit(sc->sc_dev), conf);
		sc->sc_dying = 1;
		return ENXIO;
	}

	/* the main device, ctrl endpoint */
	make_dev(&ugen_ops, UGENMINOR(device_get_unit(sc->sc_dev), 0),
		 UID_ROOT, GID_OPERATOR, 0644,
		 "%s", device_get_nameunit(sc->sc_dev));

	usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, sc->sc_udev, sc->sc_dev);
	return 0;
}

static void
ugen_make_devnodes(struct ugen_softc *sc)
{
	int endptno;
	cdev_t dev;

	for (endptno = 1; endptno < USB_MAX_ENDPOINTS; endptno++) {
		if (sc->sc_endpoints[endptno][IN].sc != NULL ||
		    sc->sc_endpoints[endptno][OUT].sc != NULL ) {
			/* endpt can be 0x81 and 0x01, representing
			 * endpoint address 0x01 and IN/OUT directions.
			 * We map both endpts to the same device,
			 * IN is reading from it, OUT is writing to it.
			 *
			 * In the if clause above we check whether one
			 * of the structs is populated.
			 */
			dev = make_dev(&ugen_ops,
				UGENMINOR(device_get_unit(sc->sc_dev), endptno),
				UID_ROOT, GID_OPERATOR, 0644,
				"%s.%d",
				device_get_nameunit(sc->sc_dev), endptno);
			if (sc->sc_endpoints[endptno][IN].sc != NULL) {
				reference_dev(dev);
				if (sc->sc_endpoints[endptno][IN].dev)
					release_dev(sc->sc_endpoints[endptno][IN].dev);
				sc->sc_endpoints[endptno][IN].dev = dev;
			}
			if (sc->sc_endpoints[endptno][OUT].sc != NULL) {
				reference_dev(dev);
				if (sc->sc_endpoints[endptno][OUT].dev)
					release_dev(sc->sc_endpoints[endptno][OUT].dev);
				sc->sc_endpoints[endptno][OUT].dev = dev;
			}
		}
	}
}

static void
ugen_destroy_devnodes(struct ugen_softc *sc)
{
	int endptno, prev_sc_dying;
	cdev_t dev;

	prev_sc_dying = sc->sc_dying;
	sc->sc_dying = 1;

	/* destroy all devices for the other (existing) endpoints as well */
	for (endptno = 1; endptno < USB_MAX_ENDPOINTS; endptno++) {
		if (sc->sc_endpoints[endptno][IN].sc != NULL ||
		    sc->sc_endpoints[endptno][OUT].sc != NULL ) {
			/* endpt can be 0x81 and 0x01, representing
			 * endpoint address 0x01 and IN/OUT directions.
			 * We map both endpoint addresses to the same device,
			 * IN is reading from it, OUT is writing to it.
			 *
			 * In the if clause above we check whether one
			 * of the structs is populated.
			 */
			dev = sc->sc_endpoints[endptno][IN].dev;
			if (dev != NULL) {
				destroy_dev(dev);
				sc->sc_endpoints[endptno][IN].dev = NULL;
			}
			dev = sc->sc_endpoints[endptno][OUT].dev;
			if (dev != NULL) {
				destroy_dev(dev);
				sc->sc_endpoints[endptno][OUT].dev = NULL;
			}
		}
	}
	sc->sc_dying = prev_sc_dying;
}

static int
ugen_set_config(struct ugen_softc *sc, int configno)
{
	usbd_device_handle dev = sc->sc_udev;
	usbd_interface_handle iface;
	usb_endpoint_descriptor_t *ed;
	struct ugen_endpoint *sce;
	u_int8_t niface, nendpt;
	int ifaceno, endptno, endpt;
	usbd_status err;
	int dir;

	DPRINTFN(1,("ugen_set_config: %s to configno %d, sc=%p\n",
		    device_get_nameunit(sc->sc_dev), configno, sc));

	ugen_destroy_devnodes(sc);

	/* We start at 1, not 0, because we don't care whether the
	 * control endpoint is open or not. It is always present.
	 */
	for (endptno = 1; endptno < USB_MAX_ENDPOINTS; endptno++) {
		if (sc->sc_is_open[endptno]) {
			DPRINTFN(1,
			     ("ugen_set_config: %s - endpoint %d is open\n",
			      device_get_nameunit(sc->sc_dev), endptno));
			return (USBD_IN_USE);
		}
	}

	/* Avoid setting the current value. */
	if (usbd_get_config_descriptor(dev)->bConfigurationValue != configno) {
		err = usbd_set_config_no(dev, configno, 1);
		if (err)
			return (err);
	}

	err = usbd_interface_count(dev, &niface);
	if (err)
		return (err);
	memset(sc->sc_endpoints, 0, sizeof sc->sc_endpoints);
	for (ifaceno = 0; ifaceno < niface; ifaceno++) {
		DPRINTFN(1,("ugen_set_config: ifaceno %d\n", ifaceno));
		err = usbd_device2interface_handle(dev, ifaceno, &iface);
		if (err)
			return (err);
		err = usbd_endpoint_count(iface, &nendpt);
		if (err)
			return (err);
		for (endptno = 0; endptno < nendpt; endptno++) {
			ed = usbd_interface2endpoint_descriptor(iface,endptno);
			endpt = ed->bEndpointAddress;
			dir = UE_GET_DIR(endpt) == UE_DIR_IN ? IN : OUT;
			sce = &sc->sc_endpoints[UE_GET_ADDR(endpt)][dir];
			DPRINTFN(1,("ugen_set_config: endptno %d, endpt=0x%02x"
				    "(%d,%d), sce=%p\n",
				    endptno, endpt, UE_GET_ADDR(endpt),
				    UE_GET_DIR(endpt), sce));
			sce->sc = sc;
			sce->edesc = ed;
			sce->iface = iface;
		}
	}

	ugen_make_devnodes(sc);

	return (USBD_NORMAL_COMPLETION);
}

int
ugenopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ugen_softc *sc;
	int unit = UGENUNIT(dev);
	int endpt = UGENENDPOINT(dev);
	usb_endpoint_descriptor_t *edesc;
	struct ugen_endpoint *sce;
	int dir, isize;
	usbd_status err;
	usbd_xfer_handle xfer;
	void *buf;
	int i, j;

	sc = devclass_get_softc(ugen_devclass, unit);
	if (sc == NULL)
		return (ENXIO);

	DPRINTFN(5, ("ugenopen: flag=%d, mode=%d, unit=%d endpt=%d\n",
		     ap->a_oflags, ap->a_devtype, unit, endpt));

	if (sc->sc_dying)
		return (ENXIO);

	if (sc->sc_is_open[endpt])
		return (EBUSY);

	if (endpt == USB_CONTROL_ENDPOINT) {
		sc->sc_is_open[USB_CONTROL_ENDPOINT] = 1;
		return (0);
	}

	/* Make sure there are pipes for all directions. */
	for (dir = OUT; dir <= IN; dir++) {
		if (ap->a_oflags & (dir == OUT ? FWRITE : FREAD)) {
			sce = &sc->sc_endpoints[endpt][dir];
			if (sce == NULL || sce->edesc == NULL)
				return (ENXIO);
		}
	}

	/* Actually open the pipes. */
	/* XXX Should back out properly if it fails. */
	for (dir = OUT; dir <= IN; dir++) {
		if (!(ap->a_oflags & (dir == OUT ? FWRITE : FREAD)))
			continue;
		sce = &sc->sc_endpoints[endpt][dir];
		sce->state = 0;
		sce->timeout = USBD_NO_TIMEOUT;
		DPRINTFN(5, ("ugenopen: sc=%p, endpt=%d, dir=%d, sce=%p\n",
			     sc, endpt, dir, sce));
		edesc = sce->edesc;
		switch (edesc->bmAttributes & UE_XFERTYPE) {
		case UE_INTERRUPT:
			if (dir == OUT) {
				err = usbd_open_pipe(sce->iface,
				    edesc->bEndpointAddress, 0, &sce->pipeh);
				if (err)
					return (EIO);
				break;
			}
			isize = UGETW(edesc->wMaxPacketSize);
			if (isize == 0)	/* shouldn't happen */
				return (EINVAL);
			sce->ibuf = kmalloc(isize, M_USBDEV, M_WAITOK);
			DPRINTFN(5, ("ugenopen: intr endpt=%d,isize=%d\n",
				     endpt, isize));
			if ((clist_alloc_cblocks(&sce->q, UGEN_IBSIZE,
						 UGEN_IBSIZE), 0) == -1)
				return (ENOMEM);
			err = usbd_open_pipe_intr(sce->iface,
				edesc->bEndpointAddress,
				USBD_SHORT_XFER_OK, &sce->pipeh, sce,
				sce->ibuf, isize, ugenintr,
				USBD_DEFAULT_INTERVAL);
			if (err) {
				kfree(sce->ibuf, M_USBDEV);
				clist_free_cblocks(&sce->q);
				return (EIO);
			}
			DPRINTFN(5, ("ugenopen: interrupt open done\n"));
			break;
		case UE_BULK:
			err = usbd_open_pipe(sce->iface,
				  edesc->bEndpointAddress, 0, &sce->pipeh);
			if (err)
				return (EIO);
			break;
		case UE_ISOCHRONOUS:
			if (dir == OUT)
				return (EINVAL);
			isize = UGETW(edesc->wMaxPacketSize);
			if (isize == 0)	/* shouldn't happen */
				return (EINVAL);
			sce->ibuf = kmalloc(isize * UGEN_NISOFRAMES,
				M_USBDEV, M_WAITOK);
			sce->cur = sce->fill = sce->ibuf;
			sce->limit = sce->ibuf + isize * UGEN_NISOFRAMES;
			DPRINTFN(5, ("ugenopen: isoc endpt=%d, isize=%d\n",
				     endpt, isize));
			err = usbd_open_pipe(sce->iface,
				  edesc->bEndpointAddress, 0, &sce->pipeh);
			if (err) {
				kfree(sce->ibuf, M_USBDEV);
				return (EIO);
			}
			for(i = 0; i < UGEN_NISOREQS; ++i) {
				sce->isoreqs[i].sce = sce;
				xfer = usbd_alloc_xfer(sc->sc_udev);
				if (xfer == 0)
					goto bad;
				sce->isoreqs[i].xfer = xfer;
				buf = usbd_alloc_buffer
					(xfer, isize * UGEN_NISORFRMS);
				if (buf == NULL) {
					i++;
					goto bad;
				}
				sce->isoreqs[i].dmabuf = buf;
				for(j = 0; j < UGEN_NISORFRMS; ++j)
					sce->isoreqs[i].sizes[j] = isize;
				usbd_setup_isoc_xfer
					(xfer, sce->pipeh, &sce->isoreqs[i],
					 sce->isoreqs[i].sizes,
					 UGEN_NISORFRMS, USBD_NO_COPY,
					 ugen_isoc_rintr);
				(void)usbd_transfer(xfer);
			}
			DPRINTFN(5, ("ugenopen: isoc open done\n"));
			break;
		bad:
			while (--i >= 0) /* implicit buffer free */
				usbd_free_xfer(sce->isoreqs[i].xfer);
			return (ENOMEM);
		case UE_CONTROL:
			sce->timeout = USBD_DEFAULT_TIMEOUT;
			return (EINVAL);
		}
	}
	sc->sc_is_open[endpt] = 1;
	return (0);
}

int
ugenclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int endpt = UGENENDPOINT(dev);
	struct ugen_softc *sc;
	struct ugen_endpoint *sce;
	int dir;
	int i;

	sc = devclass_get_softc(ugen_devclass, UGENUNIT(dev));

	DPRINTFN(5, ("ugenclose: flag=%d, mode=%d, unit=%d, endpt=%d\n",
		     ap->a_fflag, ap->a_devtype, UGENUNIT(dev), endpt));

#ifdef DIAGNOSTIC
	if (!sc->sc_is_open[endpt]) {
		kprintf("ugenclose: not open\n");
		return (EINVAL);
	}
#endif

	if (endpt == USB_CONTROL_ENDPOINT) {
		DPRINTFN(5, ("ugenclose: close control\n"));
		sc->sc_is_open[endpt] = 0;
		return (0);
	}

	for (dir = OUT; dir <= IN; dir++) {
		if (!(ap->a_fflag & (dir == OUT ? FWRITE : FREAD)))
			continue;
		sce = &sc->sc_endpoints[endpt][dir];
		if (sce->pipeh == NULL)
			continue;
		DPRINTFN(5, ("ugenclose: endpt=%d dir=%d sce=%p\n",
			     endpt, dir, sce));

		usbd_abort_pipe(sce->pipeh);
		usbd_close_pipe(sce->pipeh);
		sce->pipeh = NULL;

		switch (sce->edesc->bmAttributes & UE_XFERTYPE) {
		case UE_INTERRUPT:
			ndflush(&sce->q, sce->q.c_cc);
			clist_free_cblocks(&sce->q);
			break;
		case UE_ISOCHRONOUS:
			for (i = 0; i < UGEN_NISOREQS; ++i)
				usbd_free_xfer(sce->isoreqs[i].xfer);
		default:
			break;
		}

		if (sce->ibuf != NULL) {
			kfree(sce->ibuf, M_USBDEV);
			sce->ibuf = NULL;
			clist_free_cblocks(&sce->q);
		}
	}
	sc->sc_is_open[endpt] = 0;

	return (0);
}

static int
ugen_do_read(struct ugen_softc *sc, int endpt, struct uio *uio, int flag)
{
	struct ugen_endpoint *sce = &sc->sc_endpoints[endpt][IN];
	u_int32_t n, tn;
	char *buf;
	usbd_xfer_handle xfer;
	usbd_status err;
	int error = 0;
	int ugen_bbsize;
	u_char buffer[UGEN_CHUNK];

	DPRINTFN(5, ("%s: ugenread: %d\n", device_get_nameunit(sc->sc_dev), endpt));

	if (sc->sc_dying)
		return (EIO);

	if (endpt == USB_CONTROL_ENDPOINT)
		return (ENODEV);

#ifdef DIAGNOSTIC
	if (sce->edesc == NULL) {
		kprintf("ugenread: no edesc\n");
		return (EIO);
	}
	if (sce->pipeh == NULL) {
		kprintf("ugenread: no pipe\n");
		return (EIO);
	}
#endif

	buf = getugenbuf(ugen_bufsize, &ugen_bbsize);

	switch (sce->edesc->bmAttributes & UE_XFERTYPE) {
	case UE_INTERRUPT:
		/* Block until activity occurred. */
		crit_enter();
		while (sce->q.c_cc == 0) {
			if (flag & IO_NDELAY) {
				crit_exit();
				error = EWOULDBLOCK;
				goto done;
			}
			sce->state |= UGEN_ASLP;
			DPRINTFN(5, ("ugenread: sleep on %p\n", sce));
			error = tsleep(sce, PCATCH, "ugenri",
			    (sce->timeout * hz + 999) / 1000);
			sce->state &= ~UGEN_ASLP;
			DPRINTFN(5, ("ugenread: woke, error=%d\n", error));
			if (sc->sc_dying)
				error = EIO;
			if (error == EAGAIN) {
				error = 0;	/* timeout, return 0 bytes */
				break;
			}
			if (error)
				break;
		}
		crit_exit();

		/* Transfer as many chunks as possible. */
		while (sce->q.c_cc > 0 && uio->uio_resid > 0 && !error) {
			n = szmin(sce->q.c_cc, uio->uio_resid);
			if (n > sizeof(buffer))
				n = sizeof(buffer);

			/* Remove a small chunk from the input queue. */
			q_to_b(&sce->q, buffer, n);
			DPRINTFN(5, ("ugenread: got %d chars\n", n));

			/* Copy the data to the user process. */
			error = uiomove(buffer, n, uio);
			if (error)
				break;
		}
		break;
	case UE_BULK:
		xfer = usbd_alloc_xfer(sc->sc_udev);
		if (xfer == 0) {
			error = ENOMEM;
			goto done;
		}
		while ((n = szmin(ugen_bbsize, uio->uio_resid)) != 0) {
			DPRINTFN(1, ("ugenread: start transfer %d bytes\n",n));
			tn = n;
			err = usbd_bulk_transfer(
				xfer, sce->pipeh,
				sce->state & UGEN_SHORT_OK ?
				    USBD_SHORT_XFER_OK : 0,
				sce->timeout, buf, &tn, "ugenrb");
			if (err) {
				if (err == USBD_INTERRUPTED)
					error = EINTR;
				else if (err == USBD_TIMEOUT)
					error = ETIMEDOUT;
				else
					error = EIO;
				break;
			}
			DPRINTFN(1, ("ugenread: got %d bytes\n", tn));
			error = uiomove(buf, tn, uio);
			if (error || tn < n)
				break;
		}
		usbd_free_xfer(xfer);
		break;
	case UE_ISOCHRONOUS:
		crit_enter();
		while (sce->cur == sce->fill) {
			if (flag & IO_NDELAY) {
				crit_exit();
				error = EWOULDBLOCK;
				goto done;
			}
			sce->state |= UGEN_ASLP;
			DPRINTFN(5, ("ugenread: sleep on %p\n", sce));
			error = tsleep(sce, PCATCH, "ugenri",
			    (sce->timeout * hz + 999) / 1000);
			sce->state &= ~UGEN_ASLP;
			DPRINTFN(5, ("ugenread: woke, error=%d\n", error));
			if (sc->sc_dying)
				error = EIO;
			if (error == EAGAIN) {
				error = 0;	/* timeout, return 0 bytes */
				break;
			}
			if (error)
				break;
		}

		while (sce->cur != sce->fill && uio->uio_resid > 0 && !error) {
			if (sce->fill > sce->cur)
				n = szmin(sce->fill - sce->cur, uio->uio_resid);
			else
				n = szmin(sce->limit- sce->cur, uio->uio_resid);

			DPRINTFN(5, ("ugenread: isoc got %d chars\n", n));

			/* Copy the data to the user process. */
			error = uiomove(sce->cur, n, uio);
			if (error)
				break;
			sce->cur += n;
			if(sce->cur >= sce->limit)
				sce->cur = sce->ibuf;
		}
		crit_exit();
		break;


	default:
		error = ENXIO;
		break;
	}
done:
	relugenbuf(buf, ugen_bbsize);
	return (error);
}

int
ugenread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int endpt = UGENENDPOINT(dev);
	struct ugen_softc *sc;
	int error;

	sc = devclass_get_softc(ugen_devclass, UGENUNIT(dev));

	if (sc->sc_dying)
		return (EIO);

	sc->sc_refcnt++;
	error = ugen_do_read(sc, endpt, ap->a_uio, ap->a_ioflag);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(sc->sc_dev);
	return (error);
}

static int
ugen_do_write(struct ugen_softc *sc, int endpt, struct uio *uio, int flag)
{
	struct ugen_endpoint *sce = &sc->sc_endpoints[endpt][OUT];
	u_int32_t n;
	int error = 0;
	int ugen_bbsize;
	char *buf;
	usbd_xfer_handle xfer;
	usbd_status err;

	DPRINTFN(5, ("%s: ugenwrite: %d\n", device_get_nameunit(sc->sc_dev), endpt));

	if (sc->sc_dying)
		return (EIO);

	if (endpt == USB_CONTROL_ENDPOINT)
		return (ENODEV);

#ifdef DIAGNOSTIC
	if (sce->edesc == NULL) {
		kprintf("ugenwrite: no edesc\n");
		return (EIO);
	}
	if (sce->pipeh == NULL) {
		kprintf("ugenwrite: no pipe\n");
		return (EIO);
	}
#endif

	buf = getugenbuf(ugen_bufsize, &ugen_bbsize);

	switch (sce->edesc->bmAttributes & UE_XFERTYPE) {
	case UE_BULK:
		xfer = usbd_alloc_xfer(sc->sc_udev);
		if (xfer == 0) {
			error = EIO;
			goto done;
		}
		while ((n = szmin(ugen_bbsize, uio->uio_resid)) != 0) {
			error = uiomove(buf, n, uio);
			if (error)
				break;
			DPRINTFN(1, ("ugenwrite: transfer %d bytes\n", n));
			err = usbd_bulk_transfer(xfer, sce->pipeh, 0,
				  sce->timeout, buf, &n,"ugenwb");
			if (err) {
				if (err == USBD_INTERRUPTED)
					error = EINTR;
				else if (err == USBD_TIMEOUT)
					error = ETIMEDOUT;
				else
					error = EIO;
				break;
			}
		}
		usbd_free_xfer(xfer);
		break;
	case UE_INTERRUPT:
		xfer = usbd_alloc_xfer(sc->sc_udev);
		if (xfer == 0) {
			error = EIO;
			goto done;
		}
		while ((n = szmin(UGETW(sce->edesc->wMaxPacketSize),
				uio->uio_resid)) != 0) {
			error = uiomove(buf, n, uio);
			if (error)
				break;
			DPRINTFN(1, ("ugenwrite: transfer %d bytes\n", n));
			err = usbd_intr_transfer(xfer, sce->pipeh, 0,
				  sce->timeout, buf, &n,"ugenwi");
			if (err) {
				if (err == USBD_INTERRUPTED)
					error = EINTR;
				else if (err == USBD_TIMEOUT)
					error = ETIMEDOUT;
				else
					error = EIO;
				break;
			}
		}
		usbd_free_xfer(xfer);
		break;
	default:
		error = ENXIO;
		break;
	}
done:
	relugenbuf(buf, ugen_bbsize);
	return (error);
}

int
ugenwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int endpt = UGENENDPOINT(dev);
	struct ugen_softc *sc;
	int error;

	sc = devclass_get_softc(ugen_devclass, UGENUNIT(dev));

	if (sc->sc_dying)
		return (EIO);

	sc->sc_refcnt++;
	error = ugen_do_write(sc, endpt, ap->a_uio, ap->a_ioflag);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(sc->sc_dev);
	return (error);
}

static int
ugen_detach(device_t self)
{
	struct ugen_softc *sc = device_get_softc(self);
	struct ugen_endpoint *sce;
	int i, dir;

	DPRINTF(("ugen_detach: sc=%p\n", sc));

	sc->sc_dying = 1;
	/* Abort all pipes.  Causes processes waiting for transfer to wake. */
	for (i = 0; i < USB_MAX_ENDPOINTS; i++) {
		for (dir = OUT; dir <= IN; dir++) {
			sce = &sc->sc_endpoints[i][dir];
			if (sce && sce->pipeh)
				usbd_abort_pipe(sce->pipeh);
			KNOTE(&sce->rkq.ki_note, 0);
		}
	}
	crit_enter();
	if (--sc->sc_refcnt >= 0) {
		/* Wake everyone */
		for (i = 0; i < USB_MAX_ENDPOINTS; i++)
			wakeup(&sc->sc_endpoints[i][IN]);
		/* Wait for processes to go away. */
		usb_detach_wait(sc->sc_dev);
	}
	crit_exit();

	/* destroy the device for the control endpoint */
	ugen_destroy_devnodes(sc);
	dev_ops_remove_minor(&ugen_ops,
		    /*UGENUNITMASK,*/ UGENMINOR(device_get_unit(sc->sc_dev), 0));
	usbd_add_drv_event(USB_EVENT_DRIVER_DETACH, sc->sc_udev, sc->sc_dev);
	return (0);
}

static void
ugenintr(usbd_xfer_handle xfer, usbd_private_handle addr, usbd_status status)
{
	struct ugen_endpoint *sce = addr;
	/*struct ugen_softc *sc = sce->sc;*/
	u_int32_t count;
	u_char *ibuf;

	if (status == USBD_CANCELLED)
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF(("ugenintr: status=%d\n", status));
		if (status == USBD_STALLED)
		    usbd_clear_endpoint_stall_async(sce->pipeh);
		return;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &count, NULL);
	ibuf = sce->ibuf;

	DPRINTFN(5, ("ugenintr: xfer=%p status=%d count=%d\n",
		     xfer, status, count));
	DPRINTFN(5, ("          data = %02x %02x %02x\n",
		     ibuf[0], ibuf[1], ibuf[2]));

	(void)b_to_q(ibuf, count, &sce->q);

	if (sce->state & UGEN_ASLP) {
		sce->state &= ~UGEN_ASLP;
		DPRINTFN(5, ("ugen_intr: waking %p\n", sce));
		wakeup(sce);
	}
	KNOTE(&sce->rkq.ki_note, 0);
}

static void
ugen_isoc_rintr(usbd_xfer_handle xfer, usbd_private_handle addr,
		usbd_status status)
{
	struct isoreq *req = addr;
	struct ugen_endpoint *sce = req->sce;
	u_int32_t count, n;
	int i, isize;

	/* Return if we are aborting. */
	if (status == USBD_CANCELLED)
		return;

	usbd_get_xfer_status(xfer, NULL, NULL, &count, NULL);
	DPRINTFN(5,("ugen_isoc_rintr: xfer %d, count=%d\n",
		    (int)(req - sce->isoreqs),
		    count));

	/* throw away oldest input if the buffer is full */
	if(sce->fill < sce->cur && sce->cur <= sce->fill + count) {
		sce->cur += count;
		if(sce->cur >= sce->limit)
			sce->cur = sce->ibuf + (sce->limit - sce->cur);
		DPRINTFN(5, ("ugen_isoc_rintr: throwing away %d bytes\n",
			     count));
	}

	isize = UGETW(sce->edesc->wMaxPacketSize);
	for (i = 0; i < UGEN_NISORFRMS; i++) {
		u_int32_t actlen = req->sizes[i];
		char const *buf = (char const *)req->dmabuf + isize * i;

		/* copy data to buffer */
		while (actlen > 0) {
			n = min(actlen, sce->limit - sce->fill);
			memcpy(sce->fill, buf, n);

			buf += n;
			actlen -= n;
			sce->fill += n;
			if(sce->fill == sce->limit)
				sce->fill = sce->ibuf;
		}

		/* setup size for next transfer */
		req->sizes[i] = isize;
	}

	usbd_setup_isoc_xfer(xfer, sce->pipeh, req, req->sizes, UGEN_NISORFRMS,
			     USBD_NO_COPY, ugen_isoc_rintr);
	(void)usbd_transfer(xfer);

	if (sce->state & UGEN_ASLP) {
		sce->state &= ~UGEN_ASLP;
		DPRINTFN(5, ("ugen_isoc_rintr: waking %p\n", sce));
		wakeup(sce);
	}
	KNOTE(&sce->rkq.ki_note, 0);
}

static usbd_status
ugen_set_interface(struct ugen_softc *sc, int ifaceidx, int altno)
{
	usbd_interface_handle iface;
	usb_endpoint_descriptor_t *ed;
	usbd_status err;
	struct ugen_endpoint *sce;
	u_int8_t niface, nendpt, endptno, endpt;
	int dir;

	DPRINTFN(15, ("ugen_set_interface %d %d\n", ifaceidx, altno));

	err = usbd_interface_count(sc->sc_udev, &niface);
	if (err)
		return (err);
	if (ifaceidx < 0 || ifaceidx >= niface)
		return (USBD_INVAL);

	err = usbd_device2interface_handle(sc->sc_udev, ifaceidx, &iface);
	if (err)
		return (err);
	err = usbd_endpoint_count(iface, &nendpt);
	if (err)
		return (err);

	/* destroy the existing devices, we remake the new ones in a moment */
	ugen_destroy_devnodes(sc);

	/* XXX should only do this after setting new altno has succeeded */
	for (endptno = 0; endptno < nendpt; endptno++) {
		ed = usbd_interface2endpoint_descriptor(iface,endptno);
		endpt = ed->bEndpointAddress;
		dir = UE_GET_DIR(endpt) == UE_DIR_IN ? IN : OUT;
		sce = &sc->sc_endpoints[UE_GET_ADDR(endpt)][dir];
		sce->sc = NULL;
		sce->edesc = NULL;
		sce->iface = 0;
	}

	/* change setting */
	err = usbd_set_interface(iface, altno);
	if (err)
		return (err);

	err = usbd_endpoint_count(iface, &nendpt);
	if (err)
		return (err);
	for (endptno = 0; endptno < nendpt; endptno++) {
		ed = usbd_interface2endpoint_descriptor(iface,endptno);
		endpt = ed->bEndpointAddress;
		dir = UE_GET_DIR(endpt) == UE_DIR_IN ? IN : OUT;
		sce = &sc->sc_endpoints[UE_GET_ADDR(endpt)][dir];
		sce->sc = sc;
		sce->edesc = ed;
		sce->iface = iface;
	}

	/* make the new devices */
	ugen_make_devnodes(sc);

	return (0);
}

/* Retrieve a complete descriptor for a certain device and index. */
static usb_config_descriptor_t *
ugen_get_cdesc(struct ugen_softc *sc, int index, int *lenp)
{
	usb_config_descriptor_t *cdesc, *tdesc, cdescr;
	int len;
	usbd_status err;

	if (index == USB_CURRENT_CONFIG_INDEX) {
		tdesc = usbd_get_config_descriptor(sc->sc_udev);
		len = UGETW(tdesc->wTotalLength);
		if (lenp)
			*lenp = len;
		cdesc = kmalloc(len, M_TEMP, M_INTWAIT);
		memcpy(cdesc, tdesc, len);
		DPRINTFN(5,("ugen_get_cdesc: current, len=%d\n", len));
	} else {
		err = usbd_get_config_desc(sc->sc_udev, index, &cdescr);
		if (err)
			return (0);
		len = UGETW(cdescr.wTotalLength);
		DPRINTFN(5,("ugen_get_cdesc: index=%d, len=%d\n", index, len));
		if (lenp)
			*lenp = len;
		cdesc = kmalloc(len, M_TEMP, M_INTWAIT);
		err = usbd_get_config_desc_full(sc->sc_udev, index, cdesc, len);
		if (err) {
			kfree(cdesc, M_TEMP);
			return (0);
		}
	}
	return (cdesc);
}

static int
ugen_get_alt_index(struct ugen_softc *sc, int ifaceidx)
{
	usbd_interface_handle iface;
	usbd_status err;

	err = usbd_device2interface_handle(sc->sc_udev, ifaceidx, &iface);
	if (err)
		return (-1);
	return (usbd_get_interface_altindex(iface));
}

static int
ugen_do_ioctl(struct ugen_softc *sc, int endpt, u_long cmd,
	      caddr_t addr, int flag)
{
	struct ugen_endpoint *sce;
	usbd_status err;
	usbd_interface_handle iface;
	struct usb_config_desc *cd;
	usb_config_descriptor_t *cdesc;
	struct usb_interface_desc *id;
	usb_interface_descriptor_t *idesc;
	struct usb_endpoint_desc *ed;
	usb_endpoint_descriptor_t *edesc;
	struct usb_alt_interface *ai;
	struct usb_string_desc *si;
	u_int8_t conf, alt;

	DPRINTFN(5, ("ugenioctl: cmd=%08lx\n", cmd));
	if (sc->sc_dying)
		return (EIO);

	switch (cmd) {
	case USB_SET_SHORT_XFER:
		/* This flag only affects read */
		if (endpt == USB_CONTROL_ENDPOINT)
			return (EINVAL);
		sce = &sc->sc_endpoints[endpt][IN];

		if (sce->pipeh == NULL) {
			kprintf("ugenioctl: USB_SET_SHORT_XFER, no pipe\n");
			return (EIO);
		}

		if (*(int *)addr)
			sce->state |= UGEN_SHORT_OK;
		else
			sce->state &= ~UGEN_SHORT_OK;
		return (0);
	case USB_SET_TIMEOUT:
		sce = &sc->sc_endpoints[endpt][IN];
		sce->timeout = *(int *)addr;
		sce = &sc->sc_endpoints[endpt][OUT];
		sce->timeout = *(int *)addr;
		return (0);
	default:
		break;
	}

	if (endpt != USB_CONTROL_ENDPOINT)
		return (EINVAL);

	switch (cmd) {
#ifdef USB_DEBUG
	case USB_SETDEBUG:
		ugendebug = *(int *)addr;
		break;
#endif
	case USB_GET_CONFIG:
		err = usbd_get_config(sc->sc_udev, &conf);
		if (err)
			return (EIO);
		*(int *)addr = conf;
		break;
	case USB_SET_CONFIG:
		if (!(flag & FWRITE))
			return (EPERM);
		err = ugen_set_config(sc, *(int *)addr);
		switch (err) {
		case USBD_NORMAL_COMPLETION:
			break;
		case USBD_IN_USE:
			return (EBUSY);
		default:
			return (EIO);
		}
		break;
	case USB_GET_ALTINTERFACE:
		ai = (struct usb_alt_interface *)addr;
		err = usbd_device2interface_handle(sc->sc_udev,
			  ai->uai_interface_index, &iface);
		if (err)
			return (EINVAL);
		idesc = usbd_get_interface_descriptor(iface);
		if (idesc == NULL)
			return (EIO);
		ai->uai_alt_no = idesc->bAlternateSetting;
		break;
	case USB_SET_ALTINTERFACE:
		if (!(flag & FWRITE))
			return (EPERM);
		ai = (struct usb_alt_interface *)addr;
		err = usbd_device2interface_handle(sc->sc_udev,
			  ai->uai_interface_index, &iface);
		if (err)
			return (EINVAL);
		err = ugen_set_interface(sc, ai->uai_interface_index, ai->uai_alt_no);
		if (err)
			return (EINVAL);
		break;
	case USB_GET_NO_ALT:
		ai = (struct usb_alt_interface *)addr;
		cdesc = ugen_get_cdesc(sc, ai->uai_config_index, 0);
		if (cdesc == NULL)
			return (EINVAL);
		idesc = usbd_find_idesc(cdesc, ai->uai_interface_index, 0);
		if (idesc == NULL) {
			kfree(cdesc, M_TEMP);
			return (EINVAL);
		}
		ai->uai_alt_no = usbd_get_no_alts(cdesc, idesc->bInterfaceNumber);
		kfree(cdesc, M_TEMP);
		break;
	case USB_GET_DEVICE_DESC:
		*(usb_device_descriptor_t *)addr =
			*usbd_get_device_descriptor(sc->sc_udev);
		break;
	case USB_GET_CONFIG_DESC:
		cd = (struct usb_config_desc *)addr;
		cdesc = ugen_get_cdesc(sc, cd->ucd_config_index, 0);
		if (cdesc == NULL)
			return (EINVAL);
		cd->ucd_desc = *cdesc;
		kfree(cdesc, M_TEMP);
		break;
	case USB_GET_INTERFACE_DESC:
		id = (struct usb_interface_desc *)addr;
		cdesc = ugen_get_cdesc(sc, id->uid_config_index, 0);
		if (cdesc == NULL)
			return (EINVAL);
		if (id->uid_config_index == USB_CURRENT_CONFIG_INDEX &&
		    id->uid_alt_index == USB_CURRENT_ALT_INDEX)
			alt = ugen_get_alt_index(sc, id->uid_interface_index);
		else
			alt = id->uid_alt_index;
		idesc = usbd_find_idesc(cdesc, id->uid_interface_index, alt);
		if (idesc == NULL) {
			kfree(cdesc, M_TEMP);
			return (EINVAL);
		}
		id->uid_desc = *idesc;
		kfree(cdesc, M_TEMP);
		break;
	case USB_GET_ENDPOINT_DESC:
		ed = (struct usb_endpoint_desc *)addr;
		cdesc = ugen_get_cdesc(sc, ed->ued_config_index, 0);
		if (cdesc == NULL)
			return (EINVAL);
		if (ed->ued_config_index == USB_CURRENT_CONFIG_INDEX &&
		    ed->ued_alt_index == USB_CURRENT_ALT_INDEX)
			alt = ugen_get_alt_index(sc, ed->ued_interface_index);
		else
			alt = ed->ued_alt_index;
		edesc = usbd_find_edesc(cdesc, ed->ued_interface_index,
					alt, ed->ued_endpoint_index);
		if (edesc == NULL) {
			kfree(cdesc, M_TEMP);
			return (EINVAL);
		}
		ed->ued_desc = *edesc;
		kfree(cdesc, M_TEMP);
		break;
	case USB_GET_FULL_DESC:
	{
		int len;
		struct iovec iov;
		struct uio uio;
		struct usb_full_desc *fd = (struct usb_full_desc *)addr;
		int error;

		cdesc = ugen_get_cdesc(sc, fd->ufd_config_index, &len);
		if (len > fd->ufd_size)
			len = fd->ufd_size;
		iov.iov_base = (caddr_t)fd->ufd_data;
		iov.iov_len = len;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_resid = len;
		uio.uio_offset = 0;
		uio.uio_segflg = UIO_USERSPACE;
		uio.uio_rw = UIO_READ;
		uio.uio_td = curthread;
		error = uiomove((void *)cdesc, len, &uio);
		kfree(cdesc, M_TEMP);
		return (error);
	}
	case USB_GET_STRING_DESC:
	{
		int size;

		si = (struct usb_string_desc *)addr;
		err = usbd_get_string_desc(sc->sc_udev, si->usd_string_index,
			  si->usd_language_id, &si->usd_desc, &size);
		if (err)
			return (EINVAL);
		break;
	}
	case USB_DO_REQUEST:
	{
		struct usb_ctl_request *ur = (void *)addr;
		int len = UGETW(ur->ucr_request.wLength);
		struct iovec iov;
		struct uio uio;
		void *ptr = NULL;
		usbd_status err;
		int error = 0;

		if (!(flag & FWRITE))
			return (EPERM);
		/* Avoid requests that would damage the bus integrity. */
		if ((ur->ucr_request.bmRequestType == UT_WRITE_DEVICE &&
		     ur->ucr_request.bRequest == UR_SET_ADDRESS) ||
		    (ur->ucr_request.bmRequestType == UT_WRITE_DEVICE &&
		     ur->ucr_request.bRequest == UR_SET_CONFIG) ||
		    (ur->ucr_request.bmRequestType == UT_WRITE_INTERFACE &&
		     ur->ucr_request.bRequest == UR_SET_INTERFACE))
			return (EINVAL);

		if (len < 0 || len > 32767)
			return (EINVAL);
		if (len != 0) {
			iov.iov_base = (caddr_t)ur->ucr_data;
			iov.iov_len = len;
			uio.uio_iov = &iov;
			uio.uio_iovcnt = 1;
			uio.uio_resid = len;
			uio.uio_offset = 0;
			uio.uio_segflg = UIO_USERSPACE;
			uio.uio_rw =
				ur->ucr_request.bmRequestType & UT_READ ?
				UIO_READ : UIO_WRITE;
			uio.uio_td = curthread;
			ptr = kmalloc(len, M_TEMP, M_WAITOK);
			if (uio.uio_rw == UIO_WRITE) {
				error = uiomove(ptr, len, &uio);
				if (error)
					goto ret;
			}
		}
		sce = &sc->sc_endpoints[endpt][IN];
		err = usbd_do_request_flags(sc->sc_udev, &ur->ucr_request,
			  ptr, ur->ucr_flags, &ur->ucr_actlen, sce->timeout);
		if (err) {
			error = EIO;
			goto ret;
		}
		if (len != 0) {
			if (uio.uio_rw == UIO_READ) {
				error = uiomove(ptr, len, &uio);
				if (error)
					goto ret;
			}
		}
	ret:
		if (ptr)
			kfree(ptr, M_TEMP);
		return (error);
	}
	case USB_GET_DEVICEINFO:
		usbd_fill_deviceinfo(sc->sc_udev,
		    (struct usb_device_info *)addr, 1);
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

int
ugenioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int endpt = UGENENDPOINT(dev);
	struct ugen_softc *sc;
	int error;

	sc = devclass_get_softc(ugen_devclass, UGENUNIT(dev));
	if (sc->sc_dying)
		return (EIO);

	sc->sc_refcnt++;
	error = ugen_do_ioctl(sc, endpt, ap->a_cmd, ap->a_data, ap->a_fflag);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(sc->sc_dev);
	return (error);
}

static struct filterops ugen_filtops_read =
	{ FILTEROP_ISFD, NULL, ugen_filt_detach, ugen_filt_read };
static struct filterops ugen_filtops_write =
	{ FILTEROP_ISFD, NULL, ugen_filt_detach, ugen_filt_write };

int
ugenkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct klist *klist;
        struct ugen_softc *sc;
        struct ugen_endpoint *sce;

        sc = devclass_get_softc(ugen_devclass, UGENUNIT(dev));

	ap->a_result = 1;

	if (sc->sc_dying)
		return (0);

        /* Do not allow filter on a control endpoint */
        if (UGENENDPOINT(dev) == USB_CONTROL_ENDPOINT)
		return (0);

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		sce = &sc->sc_endpoints[UGENENDPOINT(dev)][IN];
		kn->kn_fop = &ugen_filtops_read;
		kn->kn_hook = (caddr_t)dev;
		break;
	case EVFILT_WRITE:
		sce = &sc->sc_endpoints[UGENENDPOINT(dev)][OUT];
		kn->kn_fop = &ugen_filtops_write;
		kn->kn_hook = (caddr_t)dev;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	if (sce->edesc != NULL || sce->pipeh != NULL) {
		klist = &sce->rkq.ki_note;
		knote_insert(klist, kn);
	}

	return (0);
}

static void
ugen_filt_detach(struct knote *kn)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
        struct ugen_softc *sc;
        struct ugen_endpoint *sce;
	struct klist *klist;

        sc = devclass_get_softc(ugen_devclass, UGENUNIT(dev));

	switch (kn->kn_filter) {
	case EVFILT_READ:
		sce = &sc->sc_endpoints[UGENENDPOINT(dev)][IN];
		break;
	case EVFILT_WRITE:
		sce = &sc->sc_endpoints[UGENENDPOINT(dev)][OUT];
		break;
	default:
		return;
	}

	if (sce->edesc != NULL || sce->pipeh != NULL) {
		klist = &sce->rkq.ki_note;
		knote_remove(klist, kn);
	}
}

static int
ugen_filt_read(struct knote *kn, long hint)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
	struct ugen_softc *sc;
	struct ugen_endpoint *sce;
	int ready = 0;

	sc = devclass_get_softc(ugen_devclass, UGENUNIT(dev));
	sce = &sc->sc_endpoints[UGENENDPOINT(dev)][IN];

	switch (sce->edesc->bmAttributes & UE_XFERTYPE) {
	case UE_INTERRUPT:
		if (sce->q.c_cc > 0)
			ready = 1;
		break;
	case UE_ISOCHRONOUS:
		if (sce->cur != sce->fill)
			ready = 1;
		break;
	case UE_BULK:
		ready = 1;
		break;
	default:
		break;
	}

	return (ready);
}

static int
ugen_filt_write(struct knote *kn, long hint)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
	struct ugen_softc *sc;
	struct ugen_endpoint *sce;
	int ready = 0;

	sc = devclass_get_softc(ugen_devclass, UGENUNIT(dev));
	sce = &sc->sc_endpoints[UGENENDPOINT(dev)][OUT];

	switch (sce->edesc->bmAttributes & UE_XFERTYPE) {
	case UE_INTERRUPT:
		if (sce->q.c_cc > 0)
			ready = 1;
		break;
	case UE_ISOCHRONOUS:
		if (sce->cur != sce->fill)
			ready = 1;
		break;
	case UE_BULK:
		ready = 1;
		break;
	default:
		break;
	}

	return (ready);
}

DRIVER_MODULE(ugen, uhub, ugen_driver, ugen_devclass, usbd_driver_load, NULL);
