/*
 * Copyright (c) 2000 Iwasa Kazmi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This code is based on ugen.c and ulpt.c developed by Lennart Augustsson.
 * This code includes software developed by the NetBSD Foundation, Inc. and
 * its contributors.
 */

/*
 * $FreeBSD: src/sys/dev/usb/urio.c,v 1.28 2003/08/25 22:01:06 joe Exp $
 */

/*
 * 2000/3/24  added NetBSD/OpenBSD support (from Alex Nemirovsky)
 * 2000/3/07  use two bulk-pipe handles for read and write (Dirk)
 * 2000/3/06  change major number(143), and copyright header
 *            some fix for 4.0 (Dirk)
 * 2000/3/05  codes for FreeBSD 4.x - CURRENT (Thanks to Dirk-Willem van Gulik)
 * 2000/3/01  remove retry code from urioioctl()
 *            change method of bulk transfer (no interrupt)
 * 2000/2/28  small fixes for new rio_usb.h
 * 2000/2/24  first version.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/thread2.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>

#include <bus/usb/rio500_usb.h>

#ifdef USB_DEBUG
#define DPRINTF(x)	if (uriodebug) kprintf x
#define DPRINTFN(n,x)	if (uriodebug>(n)) kprintf x
int	uriodebug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, urio, CTLFLAG_RW, 0, "USB urio");
SYSCTL_INT(_hw_usb_urio, OID_AUTO, debug, CTLFLAG_RW,
	   &uriodebug, 0, "urio debug level");
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/* difference of usbd interface */
#define USBDI 1

#define RIO_OUT 0
#define RIO_IN  1
#define RIO_NODIR  2

d_open_t  urioopen;
d_close_t urioclose;
d_read_t  urioread;
d_write_t uriowrite;
d_ioctl_t urioioctl;

static struct dev_ops urio_ops = {
	{ "urio", 0, 0 },
	.d_open =	urioopen,
	.d_close =	urioclose,
	.d_read =	urioread,
	.d_write =	uriowrite,
 	.d_ioctl =	urioioctl,
};
#define RIO_UE_GET_DIR(p) ((UE_GET_DIR(p) == UE_DIR_IN) ? RIO_IN :\
		 	  ((UE_GET_DIR(p) == UE_DIR_OUT) ? RIO_OUT :\
			    				   RIO_NODIR))

#define	URIO_BBSIZE	1024

struct urio_softc {
 	device_t sc_dev;
	usbd_device_handle sc_udev;
	usbd_interface_handle sc_iface;

	int sc_opened;
	usbd_pipe_handle sc_pipeh_in;
	usbd_pipe_handle sc_pipeh_out;
	int sc_epaddr[2];

	int sc_refcnt;
};

#define URIOUNIT(n) (minor(n))

#define RIO_RW_TIMEOUT 4000	/* ms */

static device_probe_t urio_match;
static device_attach_t urio_attach;
static device_detach_t urio_detach;

static devclass_t urio_devclass;

static kobj_method_t urio_methods[] = {
	DEVMETHOD(device_probe, urio_match),
	DEVMETHOD(device_attach, urio_attach),
	DEVMETHOD(device_detach, urio_detach),
	DEVMETHOD_END
};

static driver_t urio_driver = {
	"urio",
	urio_methods,
	sizeof(struct urio_softc)
};

static const struct usb_devno urio_devs[] = {
	{ USB_DEVICE(0x045a, 0x5001) }, /* Diamond Multimedia Rio 600 */
	{ USB_DEVICE(0x045a, 0x5002) }, /* Diamond Multimedia Rio 800 */
	{ USB_DEVICE(0x0841, 0x0001) }, /* Diamond Multimedia Rio 500 */
};

MODULE_DEPEND(urio, usb, 1, 1, 1);

static int
urio_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	DPRINTFN(10,("urio_match\n"));
	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	return (usb_lookup(urio_devs, uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

static int
urio_attach(device_t self)
{
	struct urio_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usbd_interface_handle iface;
	u_int8_t epcount;
	usbd_status r;
	char * ermsg = "<none>";
	int i;

	DPRINTFN(10,("urio_attach: sc=%p\n", sc));
	sc->sc_dev = self;
	sc->sc_udev = uaa->device;

 	if ((!uaa->device) || (!uaa->iface)) {
		ermsg = "device or iface";
 		goto nobulk;
	}
	sc->sc_iface = iface = uaa->iface;
	sc->sc_opened = 0;
	sc->sc_pipeh_in = 0;
	sc->sc_pipeh_out = 0;
	sc->sc_refcnt = 0;

	r = usbd_endpoint_count(iface, &epcount);
	if (r != USBD_NORMAL_COMPLETION) {
		ermsg = "endpoints";
		goto nobulk;
	}

	sc->sc_epaddr[RIO_OUT] = 0xff;
	sc->sc_epaddr[RIO_IN] = 0x00;

	for (i = 0; i < epcount; i++) {
		usb_endpoint_descriptor_t *edesc =
			usbd_interface2endpoint_descriptor(iface, i);
		int d;

		if (!edesc) {
			ermsg = "interface endpoint";
			goto nobulk;
		}

		d = RIO_UE_GET_DIR(edesc->bEndpointAddress);
		if (d != RIO_NODIR)
			sc->sc_epaddr[d] = edesc->bEndpointAddress;
	}
	if ( sc->sc_epaddr[RIO_OUT] == 0xff ||
	     sc->sc_epaddr[RIO_IN] == 0x00) {
		ermsg = "Rio I&O";
		goto nobulk;
	}

	make_dev(&urio_ops, device_get_unit(self),
		 UID_ROOT, GID_OPERATOR,
		 0644, "urio%d", device_get_unit(self));

	DPRINTFN(10, ("urio_attach: %p\n", sc->sc_udev));

	return 0;

 nobulk:
	kprintf("%s: could not find %s\n", device_get_nameunit(sc->sc_dev),ermsg);
	return ENXIO;
}


int
urioopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
#if (USBDI >= 1)
	struct urio_softc * sc;
#endif
	int unit = URIOUNIT(dev);
	sc = devclass_get_softc(urio_devclass, unit);
	if (sc == NULL)
		return (ENXIO);

	DPRINTFN(5, ("urioopen: flag=%d, mode=%d, unit=%d\n",
		     ap->a_oflags, ap->a_devtype, unit));

	if (sc->sc_opened)
		return EBUSY;

	if ((ap->a_oflags & (FWRITE|FREAD)) != (FWRITE|FREAD))
		return EACCES;

	sc->sc_opened = 1;
	sc->sc_pipeh_in = 0;
	sc->sc_pipeh_out = 0;
	if (usbd_open_pipe(sc->sc_iface,
		sc->sc_epaddr[RIO_IN], 0, &sc->sc_pipeh_in)
	   		!= USBD_NORMAL_COMPLETION)
	{
			sc->sc_pipeh_in = 0;
			return EIO;
	}
	if (usbd_open_pipe(sc->sc_iface,
		sc->sc_epaddr[RIO_OUT], 0, &sc->sc_pipeh_out)
	   		!= USBD_NORMAL_COMPLETION)
	{
			usbd_close_pipe(sc->sc_pipeh_in);
			sc->sc_pipeh_in = 0;
			sc->sc_pipeh_out = 0;
			return EIO;
	}
	return 0;
}

int
urioclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
#if (USBDI >= 1)
	struct urio_softc * sc;
#endif
	int unit = URIOUNIT(dev);
	sc = devclass_get_softc(urio_devclass, unit);

	DPRINTFN(5, ("urioclose: flag=%d, mode=%d, unit=%d\n",
		ap->a_fflag, ap->a_devtype, unit));
	if (sc->sc_pipeh_in)
		usbd_close_pipe(sc->sc_pipeh_in);

	if (sc->sc_pipeh_out)
		usbd_close_pipe(sc->sc_pipeh_out);

	sc->sc_pipeh_in = 0;
	sc->sc_pipeh_out = 0;
	sc->sc_opened = 0;
	sc->sc_refcnt = 0;
	return 0;
}

int
urioread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
#if (USBDI >= 1)
	struct urio_softc * sc;
	usbd_xfer_handle reqh;
#else
	usbd_request_handle reqh;
	usbd_private_handle r_priv;
        void *r_buff;
        usbd_status r_status;
#endif
	int unit = URIOUNIT(dev);
	usbd_status r;
	char buf[URIO_BBSIZE];
	u_int32_t n, tn;
	int error = 0;

	sc = devclass_get_softc(urio_devclass, unit);

	DPRINTFN(5, ("urioread: %d\n", unit));
	if (!sc->sc_opened)
		return EIO;

#if (USBDI >= 1)
	sc->sc_refcnt++;
	reqh = usbd_alloc_xfer(sc->sc_udev);
#else
	reqh = usbd_alloc_request();
#endif
	if (reqh == 0)
		return ENOMEM;
	while ((n = szmin(URIO_BBSIZE, uio->uio_resid)) != 0) {
		DPRINTFN(1, ("urioread: start transfer %d bytes\n", n));
		tn = n;
#if (USBDI >= 1)
 		usbd_setup_xfer(reqh, sc->sc_pipeh_in, 0, buf, tn,
				       0, RIO_RW_TIMEOUT, 0);
#else
		r = usbd_setup_request(reqh, sc->sc_pipeh_in, 0, buf, tn,
				       0, RIO_RW_TIMEOUT, 0);
		if (r != USBD_NORMAL_COMPLETION) {
			error = EIO;
			break;
		}
#endif
		r = usbd_sync_transfer(reqh);
		if (r != USBD_NORMAL_COMPLETION) {
			DPRINTFN(1, ("urioread: error=%d\n", r));
			usbd_clear_endpoint_stall(sc->sc_pipeh_in);
			tn = 0;
			error = EIO;
			break;
		}
#if (USBDI >= 1)
		usbd_get_xfer_status(reqh, 0, 0, &tn, 0);
#else
		usbd_get_request_status(reqh, &r_priv, &r_buff, &tn, &r_status);
#endif

		DPRINTFN(1, ("urioread: got %d bytes\n", tn));
		error = uiomove(buf, tn, uio);
		if (error || tn < n)
			break;
	}
#if (USBDI >= 1)
	usbd_free_xfer(reqh);
#else
	usbd_free_request(reqh);
#endif

	return error;
}

int
uriowrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
#if (USBDI >= 1)
	struct urio_softc * sc;
	usbd_xfer_handle reqh;
#else
	usbd_request_handle reqh;
#endif
	int unit = URIOUNIT(dev);
	usbd_status r;
	char buf[URIO_BBSIZE];
	u_int32_t n;
	int error = 0;

	sc = devclass_get_softc(urio_devclass, unit);

	DPRINTFN(5, ("uriowrite: %d\n", unit));
	if (!sc->sc_opened)
		return EIO;

#if (USBDI >= 1)
	sc->sc_refcnt++;
	reqh = usbd_alloc_xfer(sc->sc_udev);
#else
	reqh = usbd_alloc_request();
#endif
	if (reqh == 0)
		return EIO;
	while ((n = szmin(URIO_BBSIZE, uio->uio_resid)) != 0) {
		error = uiomove(buf, n, uio);
		if (error)
			break;
		DPRINTFN(1, ("uriowrite: transfer %d bytes\n", n));
#if (USBDI >= 1)
 		usbd_setup_xfer(reqh, sc->sc_pipeh_out, 0, buf, n,
				       0, RIO_RW_TIMEOUT, 0);
#else
		r = usbd_setup_request(reqh, sc->sc_pipeh_out, 0, buf, n,
				       0, RIO_RW_TIMEOUT, 0);
		if (r != USBD_NORMAL_COMPLETION) {
			error = EIO;
			break;
		}
#endif
		r = usbd_sync_transfer(reqh);
		if (r != USBD_NORMAL_COMPLETION) {
			DPRINTFN(1, ("uriowrite: error=%d\n", r));
			usbd_clear_endpoint_stall(sc->sc_pipeh_out);
			error = EIO;
			break;
		}
#if (USBDI >= 1)
		usbd_get_xfer_status(reqh, 0, 0, 0, 0);
#endif
	}

#if (USBDI >= 1)
	usbd_free_xfer(reqh);
#else
	usbd_free_request(reqh);
#endif

	return error;
}


int
urioioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
#if (USBDI >= 1)
	struct urio_softc * sc;
#endif
	int unit = URIOUNIT(dev);
	struct RioCommand *rio_cmd;
	int requesttype, len;
	struct iovec iov;
	struct uio uio;
	usb_device_request_t req;
	int req_flags = 0, req_actlen = 0;
	void *ptr = NULL;
	int error = 0;
	usbd_status r;

	sc = devclass_get_softc(urio_devclass, unit);

	switch (ap->a_cmd) {
	case RIO_RECV_COMMAND:
		if (!(ap->a_fflag & FWRITE))
			return EPERM;
		rio_cmd = (struct RioCommand *)ap->a_data;
		if (rio_cmd == NULL)
			return EINVAL;
		len = rio_cmd->length;

		requesttype = rio_cmd->requesttype | UT_READ_VENDOR_DEVICE;
		DPRINTFN(1,("sending command:reqtype=%0x req=%0x value=%0x index=%0x len=%0x\n",
			requesttype, rio_cmd->request, rio_cmd->value, rio_cmd->index, len));
		break;

	case RIO_SEND_COMMAND:
		if (!(ap->a_fflag & FWRITE))
			return EPERM;
		rio_cmd = (struct RioCommand *)ap->a_data;
		if (rio_cmd == NULL)
			return EINVAL;
		len = rio_cmd->length;

		requesttype = rio_cmd->requesttype | UT_WRITE_VENDOR_DEVICE;
		DPRINTFN(1,("sending command:reqtype=%0x req=%0x value=%0x index=%0x len=%0x\n",
			requesttype, rio_cmd->request, rio_cmd->value, rio_cmd->index, len));
		break;

	default:
		return EINVAL;
		break;
	}

	/* Send rio control message */
	req.bmRequestType = requesttype;
	req.bRequest = rio_cmd->request;
	USETW(req.wValue, rio_cmd->value);
	USETW(req.wIndex, rio_cmd->index);
	USETW(req.wLength, len);

	if (len < 0 || len > 32767)
		return EINVAL;
	if (len != 0) {
		iov.iov_base = (caddr_t)rio_cmd->buffer;
		iov.iov_len = len;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_resid = len;
		uio.uio_offset = 0;
		uio.uio_segflg = UIO_USERSPACE;
		uio.uio_rw =
			req.bmRequestType & UT_READ ?
			UIO_READ : UIO_WRITE;
		uio.uio_td = curthread;
		ptr = kmalloc(len, M_TEMP, M_WAITOK);
		if (uio.uio_rw == UIO_WRITE) {
			error = uiomove(ptr, len, &uio);
			if (error)
				goto ret;
		}
	}

	r = usbd_do_request_flags(sc->sc_udev, &req,
				  ptr, req_flags, &req_actlen,
				  USBD_DEFAULT_TIMEOUT);
	if (r == USBD_NORMAL_COMPLETION) {
		error = 0;
		if (len != 0) {
			if (uio.uio_rw == UIO_READ) {
				error = uiomove(ptr, len, &uio);
			}
		}
	} else {
		error = EIO;
	}

ret:
	if (ptr)
		kfree(ptr, M_TEMP);
	return error;
}

static int
urio_detach(device_t self)
{
	DPRINTF(("%s: disconnected\n", device_get_nameunit(self)));
	dev_ops_remove_minor(&urio_ops, /*-1, */device_get_unit(self));
	/* XXX not implemented yet */
	device_set_desc(self, NULL);
	return 0;
}

DRIVER_MODULE(urio, uhub, urio_driver, urio_devclass, usbd_driver_load, NULL);
