/* 
 * $NetBSD: uscanner.c,v 1.30 2002/07/11 21:14:36 augustss Exp $
 * $FreeBSD: src/sys/dev/usb/uscanner.c,v 1.48 2003/12/22 19:58:27 sanpei Exp $
 */

/* Also already merged from NetBSD:
 *	$NetBSD: uscanner.c,v 1.33 2002/09/23 05:51:24 simonb Exp $
 */

/*
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology
 * and Nick Hibma (n_hibma@qubesoft.com).
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
#include <sys/select.h>
#include <sys/proc.h>
#include <sys/event.h>
#include <sys/sysctl.h>
#include <sys/thread2.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>

#ifdef USB_DEBUG
#define DPRINTF(x)	if (uscannerdebug) kprintf x
#define DPRINTFN(n,x)	if (uscannerdebug>(n)) kprintf x
int	uscannerdebug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, uscanner, CTLFLAG_RW, 0, "USB uscanner");
SYSCTL_INT(_hw_usb_uscanner, OID_AUTO, debug, CTLFLAG_RW,
	   &uscannerdebug, 0, "uscanner debug level");
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

struct uscan_info {
	struct usb_devno devno;
	u_int flags;
#define USC_KEEP_OPEN 1
};

/* Table of scanners that may work with this driver. */
static const struct uscan_info uscanner_devs[] = {
  /* Acer Peripherals */
 {{ USB_DEVICE(0x04a5, 0x2022) }, 0 }, /* Acerscan 320U */
 {{ USB_DEVICE(0x04a5, 0x20b0) }, 0 }, /* Benq 3300U/4300U */
 {{ USB_DEVICE(0x04a5, 0x2040) }, 0 }, /* Acerscan 640U */
 {{ USB_DEVICE(0x04a5, 0x20be) }, 0 }, /* Acerscan 640BT */
 {{ USB_DEVICE(0x04a5, 0x2060) }, 0 }, /* Acerscan 620U */
 {{ USB_DEVICE(0x04a5, 0x20c0) }, 0 }, /* Acerscan 1240U */
 {{ USB_DEVICE(0x04a5, 0x12a6) }, 0 }, /* Acerscan C310U */
  /* AGFA */
 {{ USB_DEVICE(0x06bd, 0x0002) }, 0 }, /* SnapScan 1236U */
 {{ USB_DEVICE(0x06bd, 0x0001) }, 0 }, /* SnapScan 1212U */
 {{ USB_DEVICE(0x06bd, 0x2061) }, 0 }, /* SnapScan 1212U */
 {{ USB_DEVICE(0x06bd, 0x0100) }, 0 }, /* SnapScan Touch */
 {{ USB_DEVICE(0x06bd, 0x208d) }, 0 }, /* SnapScan e40 */
 {{ USB_DEVICE(0x06bd, 0x208f) }, 0 }, /* SnapScan e50 */
 {{ USB_DEVICE(0x06bd, 0x2091) }, 0 }, /* SnapScan e20 */
 {{ USB_DEVICE(0x06bd, 0x2095) }, 0 }, /* SnapScan e25 */
 {{ USB_DEVICE(0x06bd, 0x2097) }, 0 }, /* SnapScan e26 */
 {{ USB_DEVICE(0x06bd, 0x20fd) }, 0 }, /* SnapScan e52 */
  /* Avision */
 {{ USB_DEVICE(0x0638, 0x0268) }, 0 }, /* Avision 1200U */
  /* Canon */
 {{ USB_DEVICE(0x04a9, 0x2206) }, 0 }, /* CanoScan N656U */
 {{ USB_DEVICE(0x04a9, 0x220d) }, 0 }, /* CanoScan N676U */
 {{ USB_DEVICE(0x04a9, 0x2207) }, 0 }, /* CanoScan N1220U */
 {{ USB_DEVICE(0x04a9, 0x2208) }, 0 }, /* CanoScan D660U */
 {{ USB_DEVICE(0x04a9, 0x220e) }, 0 }, /* CanoScan N1240U */
 {{ USB_DEVICE(0x04a9, 0x2220) }, 0 }, /* CanoScan LIDE 25 */
  /* Kye */
 {{ USB_DEVICE(0x0458, 0x2001) }, 0 }, /* ColorPage Vivid-Pro */
  /* HP */
 {{ USB_DEVICE(0x03f0, 0x0605) }, 0 }, /* ScanJet 2200C */
 {{ USB_DEVICE(0x03f0, 0x0205) }, 0 }, /* ScanJet 3300C */
 {{ USB_DEVICE(0x03f0, 0x0405) }, 0 }, /* ScanJet 3400cse */
 {{ USB_DEVICE(0x03f0, 0x0101) }, 0 }, /* Scanjet 4100C */
 {{ USB_DEVICE(0x03f0, 0x0105) }, 0 }, /* ScanJet 4200C */
 {{ USB_DEVICE(0x03f0, 0x0305) }, 0 }, /* Scanjet 4300C */
 {{ USB_DEVICE(0x03f0, 0x3005) }, 0 }, /* ScanJet 4670v */
 {{ USB_DEVICE(0x03f0, 0x0102) }, 0 }, /* Photosmart S20 */
 {{ USB_DEVICE(0x03f0, 0x0401) }, 0 }, /* Scanjet 5200C */
 {{ USB_DEVICE(0x03f0, 0x0701) }, 0 }, /* Scanjet 5300C */
 {{ USB_DEVICE(0x03f0, 0x1005) }, 0 }, /* Scanjet 5400C */
 {{ USB_DEVICE(0x03f0, 0x0201) }, 0 }, /* ScanJet 6200C */
 {{ USB_DEVICE(0x03f0, 0x0601) }, 0 }, /* Scanjet 6300C */
 {{ USB_DEVICE(0x03f0, 0x0b01) }, 0 }, /* Scanjet 82x0C */
  /* Scanlogic */
 {{ USB_DEVICE(0x04ce, 0x0300) }, 0 }, /* Phantom 336CX - C3 */
  /* Microtek */
 {{ USB_DEVICE(0x05da, 0x0099) }, 0 }, /* Phantom 336CX - C3 */
 {{ USB_DEVICE(0x05da, 0x0094) }, 0 }, /* ScanMaker X6 - X6U */
 {{ USB_DEVICE(0x05da, 0x00a0) }, 0 }, /* Phantom 336CX - C3 */
 {{ USB_DEVICE(0x05da, 0x009a) }, 0 }, /* Phantom C6 */
 {{ USB_DEVICE(0x05da, 0x00a3) }, 0 }, /* ScanMaker V6USL */
 {{ USB_DEVICE(0x05da, 0x80a3) }, 0 }, /* ScanMaker V6USL */
 {{ USB_DEVICE(0x05da, 0x80ac) }, 0 }, /* ScanMaker V6UL */
  /* Minolta */
 {{ USB_DEVICE(0x0686, 0x400e) }, 0 }, /* Dimage 5400 */
  /* Mustek */
 {{ USB_DEVICE(0x055f, 0x0001) }, 0 }, /* 1200 CU */
 {{ USB_DEVICE(0x055f, 0x0010) }, 0 }, /* BearPaw 1200F */
 {{ USB_DEVICE(0x055f, 0x021e) }, 0 }, /* BearPaw 1200TA */
 {{ USB_DEVICE(0x055f, 0x0873) }, 0 }, /* 600 USB */
 {{ USB_DEVICE(0x055f, 0x0002) }, 0 }, /* 600 CU */
 {{ USB_DEVICE(0x055f, 0x0003) }, 0 }, /* 1200 USB */
 {{ USB_DEVICE(0x055f, 0x0006) }, 0 }, /* 1200 UB */
 {{ USB_DEVICE(0x055f, 0x0007) }, 0 }, /* 1200 USB */
 {{ USB_DEVICE(0x055f, 0x0008) }, 0 }, /* 1200 CU */
  /* National */
 {{ USB_DEVICE(0x0400, 0x1000) }, 0 }, /* BearPaw 1200 */
 {{ USB_DEVICE(0x0400, 0x1001) }, 0 }, /* BearPaw 2400 */
  /* Nikon */
 {{ USB_DEVICE(0x04b0, 0x4000) }, 0 }, /* CoolScan LS40 ED */
  /* Primax */
 {{ USB_DEVICE(0x0461, 0x0300) }, 0 }, /* G2-200 */
 {{ USB_DEVICE(0x0461, 0x0301) }, 0 }, /* G2E-300 */
 {{ USB_DEVICE(0x0461, 0x0302) }, 0 }, /* G2-300 */
 {{ USB_DEVICE(0x0461, 0x0303) }, 0 }, /* G2E-300 */
 {{ USB_DEVICE(0x0461, 0x0340) }, 0 }, /* Colorado USB 9600 */
 {{ USB_DEVICE(0x0461, 0x0341) }, 0 }, /* Colorado 600u */
 {{ USB_DEVICE(0x0461, 0x0345) }, 0 }, /* Visioneer 6200 */
 {{ USB_DEVICE(0x0461, 0x0360) }, 0 }, /* Colorado USB 19200 */
 {{ USB_DEVICE(0x0461, 0x0361) }, 0 }, /* Colorado 1200u */
 {{ USB_DEVICE(0x0461, 0x0380) }, 0 }, /* G2-600 */
 {{ USB_DEVICE(0x0461, 0x0381) }, 0 }, /* ReadyScan 636i */
 {{ USB_DEVICE(0x0461, 0x0382) }, 0 }, /* G2-600 */
 {{ USB_DEVICE(0x0461, 0x0383) }, 0 }, /* G2E-600 */
  /* Epson */
 {{ USB_DEVICE(0x04b8, 0x0101) }, 0 }, /* Perfection 636U/636Photo */
 {{ USB_DEVICE(0x04b8, 0x0103) }, 0 }, /* Perfection 610 */
 {{ USB_DEVICE(0x04b8, 0x0104) }, 0 }, /* Perfection 1200U/1200Photo */
 {{ USB_DEVICE(0x04b8, 0x010b) }, 0 }, /* Perfection 1240U/1240Photo */
 {{ USB_DEVICE(0x04b8, 0x010f) }, 0 }, /* Perfection 1250U/1250Photo */
 {{ USB_DEVICE(0x04b8, 0x0107) }, 0 }, /* Expression 1600 */
 {{ USB_DEVICE(0x04b8, 0x010a) }, 0 }, /* Perfection 1640SU */
 {{ USB_DEVICE(0x04b8, 0x010c) }, 0 }, /* Perfection 640U */
 {{ USB_DEVICE(0x04b8, 0x0110) }, 0 }, /* Perfection 1650 */
 {{ USB_DEVICE(0x04b8, 0x011e) }, 0 }, /* Perfection 1660 */
 {{ USB_DEVICE(0x04b8, 0x011d) }, 0 }, /* Perfection 1260 */
 {{ USB_DEVICE(0x04b8, 0x011f) }, 0 }, /* Perfection 1670 */
 {{ USB_DEVICE(0x04b8, 0x0120) }, 0 }, /* Perfection 1270 */
 {{ USB_DEVICE(0x04b8, 0x080f) }, 0 }, /* Stylus Photo RX425 */
 {{ USB_DEVICE(0x04b8, 0x011c) }, USC_KEEP_OPEN }, /* Perfection 3200 */
 {{ USB_DEVICE(0x04b8, 0x0112) }, USC_KEEP_OPEN }, /* GT-9700F */
 {{ USB_DEVICE(0x04b8, 0x011b) }, 0 }, /* GT-9300UF */
 {{ USB_DEVICE(0x04b8, 0x0121) }, 0 }, /* Perfection 2480 */
 {{ USB_DEVICE(0x04b8, 0x080e) }, USC_KEEP_OPEN }, /* CX-3500/3600/3650 MFP */
 {{ USB_DEVICE(0x04b8, 0x0122) }, 0 }, /* Perfection 3590 */
 {{ USB_DEVICE(0x04b8, 0x0820) }, 0 }, /* CX4200 MP */
 {{ USB_DEVICE(0x04b8, 0x012a) }, 0 }, /* Perfection 4990 Photo */
 {{ USB_DEVICE(0x04b8, 0x082b) }, 0 }, /* DX-50x0 MFP */
 {{ USB_DEVICE(0x04b8, 0x082e) }, 0 }, /* DX-60x0 MFP */
  /* UMAX */
 {{ USB_DEVICE(0x1606, 0x0010) }, 0 }, /* Astra 1220U */
 {{ USB_DEVICE(0x1606, 0x0002) }, 0 }, /* Astra 1236U */
 {{ USB_DEVICE(0x1606, 0x0030) }, 0 }, /* Astra 2000U */
 {{ USB_DEVICE(0x1606, 0x0130) }, 0 }, /* Astra 2100U */
 {{ USB_DEVICE(0x1606, 0x0230) }, 0 }, /* Astra 2200U */
 {{ USB_DEVICE(0x1606, 0x0060) }, 0 }, /* Astra 3400 */
  /* Visioneer */
 {{ USB_DEVICE(0x04a7, 0x0224) }, 0 }, /* Scanport 3000 */
 {{ USB_DEVICE(0x04a7, 0x0221) }, 0 }, /* OneTouch 5300 */
 {{ USB_DEVICE(0x04a7, 0x0211) }, 0 }, /* OneTouch 7600 */
 {{ USB_DEVICE(0x04a7, 0x0231) }, 0 }, /* OneTouch 6100 */
 {{ USB_DEVICE(0x04a7, 0x0311) }, 0 }, /* OneTouch 6200 */
 {{ USB_DEVICE(0x04a7, 0x0321) }, 0 }, /* OneTouch 8100 */
 {{ USB_DEVICE(0x04a7, 0x0331) }, 0 }, /* OneTouch 8600 */
  /* Ultima */
 {{ USB_DEVICE(0x05d8, 0x4002) }, 0 }, /* 1200 UB Plus */
};
#define uscanner_lookup(v, p) ((const struct uscan_info *)usb_lookup(uscanner_devs, v, p))

#define	USCANNER_BUFFERSIZE	1024

struct uscanner_softc {
	device_t		sc_dev;		/* base device */
	usbd_device_handle	sc_udev;
	usbd_interface_handle	sc_iface;

	u_int			sc_dev_flags;

	usbd_pipe_handle	sc_bulkin_pipe;
	int			sc_bulkin;
	usbd_xfer_handle	sc_bulkin_xfer;
	void 			*sc_bulkin_buffer;
	int			sc_bulkin_bufferlen;
	int			sc_bulkin_datalen;

	usbd_pipe_handle	sc_bulkout_pipe;
	int			sc_bulkout;
	usbd_xfer_handle	sc_bulkout_xfer;
	void 			*sc_bulkout_buffer;
	int			sc_bulkout_bufferlen;
	int			sc_bulkout_datalen;

	u_char			sc_state;
#define USCANNER_OPEN		0x01	/* opened */

	int			sc_refcnt;
	u_char			sc_dying;
};

d_open_t  uscanneropen;
d_close_t uscannerclose;
d_read_t  uscannerread;
d_write_t uscannerwrite;
d_kqfilter_t uscannerkqfilter;

static struct dev_ops uscanner_ops = {
	{ "uscanner", 0, 0 },
	.d_open =	uscanneropen,
	.d_close =	uscannerclose,
	.d_read =	uscannerread,
	.d_write =	uscannerwrite,
	.d_kqfilter =	uscannerkqfilter
};

static int uscanner_do_read(struct uscanner_softc *, struct uio *, int);
static int uscanner_do_write(struct uscanner_softc *, struct uio *, int);
static void uscanner_do_close(struct uscanner_softc *);

#define USCANNERUNIT(n) (minor(n))

static device_probe_t uscanner_match;
static device_attach_t uscanner_attach;
static device_detach_t uscanner_detach;

static devclass_t uscanner_devclass;

static kobj_method_t uscanner_methods[] = {
	DEVMETHOD(device_probe, uscanner_match),
	DEVMETHOD(device_attach, uscanner_attach),
	DEVMETHOD(device_detach, uscanner_detach),
	DEVMETHOD_END
};

static driver_t uscanner_driver = {
	"uscanner",
	uscanner_methods,
	sizeof(struct uscanner_softc)
};

MODULE_DEPEND(uscanner, usb, 1, 1, 1);

static int
uscanner_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->iface != NULL)
		return UMATCH_NONE;

	return (uscanner_lookup(uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

static int
uscanner_attach(device_t self)
{
	struct uscanner_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usb_interface_descriptor_t *id = NULL;
	usb_endpoint_descriptor_t *ed, *ed_bulkin = NULL, *ed_bulkout = NULL;
	int i;
	usbd_status err;

	sc->sc_dev = self;

	sc->sc_dev_flags = uscanner_lookup(uaa->vendor, uaa->product)->flags;

	sc->sc_udev = uaa->device;

	err = usbd_set_config_no(uaa->device, 1, 1); /* XXX */
	if (err) {
		kprintf("%s: setting config no failed\n",
		    device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}

	/* XXX We only check the first interface */
	err = usbd_device2interface_handle(sc->sc_udev, 0, &sc->sc_iface);
	if (!err && sc->sc_iface)
	    id = usbd_get_interface_descriptor(sc->sc_iface);
	if (err || id == NULL) {
		kprintf("%s: could not get interface descriptor, err=%d,id=%p\n",
		       device_get_nameunit(sc->sc_dev), err, id);
		return ENXIO;
	}

	/* Find the two first bulk endpoints */
	for (i = 0 ; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			kprintf("%s: could not read endpoint descriptor\n",
			       device_get_nameunit(sc->sc_dev));
			return ENXIO;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN
		    && (ed->bmAttributes & UE_XFERTYPE) == UE_BULK) {
			ed_bulkin = ed;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT
		    && (ed->bmAttributes & UE_XFERTYPE) == UE_BULK) {
		        ed_bulkout = ed;
		}

		if (ed_bulkin && ed_bulkout)	/* found all we need */
			break;
	}

	/* Verify that we goething sensible */
	if (ed_bulkin == NULL || ed_bulkout == NULL) {
		kprintf("%s: bulk-in and/or bulk-out endpoint not found\n",
			device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}

	sc->sc_bulkin = ed_bulkin->bEndpointAddress;
	sc->sc_bulkout = ed_bulkout->bEndpointAddress;

	/* the main device, ctrl endpoint */
	make_dev(&uscanner_ops, device_get_unit(sc->sc_dev),
		 UID_ROOT, GID_OPERATOR, 0644,
		 "%s", device_get_nameunit(sc->sc_dev));

	usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, sc->sc_udev,
			   sc->sc_dev);

	return 0;
}

int
uscanneropen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uscanner_softc *sc;
	int unit = USCANNERUNIT(dev);
	usbd_status err;

	sc = devclass_get_softc(uscanner_devclass, unit);
	if (sc == NULL)
		return (ENXIO);

	DPRINTFN(5, ("uscanneropen: flag=%d, mode=%d, unit=%d\n",
		     ap->a_oflags, ap->a_devtype, unit));

	if (sc->sc_dying)
		return (ENXIO);

	if (sc->sc_state & USCANNER_OPEN)
		return (EBUSY);

	sc->sc_state |= USCANNER_OPEN;

	sc->sc_bulkin_buffer = kmalloc(USCANNER_BUFFERSIZE, M_USBDEV, M_WAITOK);
	sc->sc_bulkout_buffer = kmalloc(USCANNER_BUFFERSIZE, M_USBDEV, M_WAITOK);
	/* No need to check buffers for NULL since we have WAITOK */

	sc->sc_bulkin_bufferlen = USCANNER_BUFFERSIZE;
	sc->sc_bulkout_bufferlen = USCANNER_BUFFERSIZE;

	/* We have decided on which endpoints to use, now open the pipes */
	if (sc->sc_bulkin_pipe == NULL) {
		err = usbd_open_pipe(sc->sc_iface, sc->sc_bulkin,
				     USBD_EXCLUSIVE_USE, &sc->sc_bulkin_pipe);
		if (err) {
			kprintf("%s: cannot open bulk-in pipe (addr %d)\n",
			       device_get_nameunit(sc->sc_dev), sc->sc_bulkin);
			uscanner_do_close(sc);
			return (EIO);
		}
	}
	if (sc->sc_bulkout_pipe == NULL) {
		err = usbd_open_pipe(sc->sc_iface, sc->sc_bulkout,
				     USBD_EXCLUSIVE_USE, &sc->sc_bulkout_pipe);
		if (err) {
			kprintf("%s: cannot open bulk-out pipe (addr %d)\n",
			       device_get_nameunit(sc->sc_dev), sc->sc_bulkout);
			uscanner_do_close(sc);
			return (EIO);
		}
	}

	sc->sc_bulkin_xfer = usbd_alloc_xfer(sc->sc_udev);
	if (sc->sc_bulkin_xfer == NULL) {
		uscanner_do_close(sc);
		return (ENOMEM);
	}
	sc->sc_bulkout_xfer = usbd_alloc_xfer(sc->sc_udev);
	if (sc->sc_bulkout_xfer == NULL) {
		uscanner_do_close(sc);
		return (ENOMEM);
	}

	return (0);	/* success */
}

int
uscannerclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uscanner_softc *sc;

	sc = devclass_get_softc(uscanner_devclass, USCANNERUNIT(dev));

	DPRINTFN(5, ("uscannerclose: flag=%d, mode=%d, unit=%d\n",
		     ap->a_fflag, ap->a_devtype, USCANNERUNIT(dev)));

#ifdef DIAGNOSTIC
	if (!(sc->sc_state & USCANNER_OPEN)) {
		kprintf("uscannerclose: not open\n");
		return (EINVAL);
	}
#endif

	uscanner_do_close(sc);

	return (0);
}

void
uscanner_do_close(struct uscanner_softc *sc)
{
	if (sc->sc_bulkin_xfer) {
		usbd_free_xfer(sc->sc_bulkin_xfer);
		sc->sc_bulkin_xfer = NULL;
	}
	if (sc->sc_bulkout_xfer) {
		usbd_free_xfer(sc->sc_bulkout_xfer);
		sc->sc_bulkout_xfer = NULL;
	}

	if (!(sc->sc_dev_flags & USC_KEEP_OPEN)) {
		if (sc->sc_bulkin_pipe != NULL) {
			usbd_abort_pipe(sc->sc_bulkin_pipe);
			usbd_close_pipe(sc->sc_bulkin_pipe);
			sc->sc_bulkin_pipe = NULL;
		}
		if (sc->sc_bulkout_pipe != NULL) {
			usbd_abort_pipe(sc->sc_bulkout_pipe);
			usbd_close_pipe(sc->sc_bulkout_pipe);
			sc->sc_bulkout_pipe = NULL;
		}
	}

	if (sc->sc_bulkin_buffer) {
		kfree(sc->sc_bulkin_buffer, M_USBDEV);
		sc->sc_bulkin_buffer = NULL;
	}
	if (sc->sc_bulkout_buffer) {
		kfree(sc->sc_bulkout_buffer, M_USBDEV);
		sc->sc_bulkout_buffer = NULL;
	}

	sc->sc_state &= ~USCANNER_OPEN;
}

static int
uscanner_do_read(struct uscanner_softc *sc, struct uio *uio, int flag)
{
	u_int32_t n, tn;
	usbd_status err;
	int error = 0;

	DPRINTFN(5, ("%s: uscannerread\n", device_get_nameunit(sc->sc_dev)));

	if (sc->sc_dying)
		return (EIO);

	while ((n = szmin(sc->sc_bulkin_bufferlen, uio->uio_resid)) != 0) {
		DPRINTFN(1, ("uscannerread: start transfer %d bytes\n",n));
		tn = n;

		err = usbd_bulk_transfer(
			sc->sc_bulkin_xfer, sc->sc_bulkin_pipe,
			USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT,
			sc->sc_bulkin_buffer, &tn,
			"uscnrb");
		if (err) {
			if (err == USBD_INTERRUPTED)
				error = EINTR;
			else if (err == USBD_TIMEOUT)
				error = ETIMEDOUT;
			else
				error = EIO;
			break;
		}
		DPRINTFN(1, ("uscannerread: got %d bytes\n", tn));
		error = uiomove(sc->sc_bulkin_buffer, tn, uio);
		if (error || tn < n)
			break;
	}

	return (error);
}

int
uscannerread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uscanner_softc *sc;
	int error;

	sc = devclass_get_softc(uscanner_devclass, USCANNERUNIT(dev));

	sc->sc_refcnt++;
	error = uscanner_do_read(sc, ap->a_uio, ap->a_ioflag);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(sc->sc_dev);

	return (error);
}

static int
uscanner_do_write(struct uscanner_softc *sc, struct uio *uio, int flag)
{
	u_int32_t n;
	int error = 0;
	usbd_status err;

	DPRINTFN(5, ("%s: uscanner_do_write\n", device_get_nameunit(sc->sc_dev)));

	if (sc->sc_dying)
		return (EIO);

	while ((n = szmin(sc->sc_bulkout_bufferlen, uio->uio_resid)) != 0) {
		error = uiomove(sc->sc_bulkout_buffer, n, uio);
		if (error)
			break;
		DPRINTFN(1, ("uscanner_do_write: transfer %d bytes\n", n));
		err = usbd_bulk_transfer(
			sc->sc_bulkout_xfer, sc->sc_bulkout_pipe,
			0, USBD_NO_TIMEOUT,
			sc->sc_bulkout_buffer, &n,
			"uscnwb");
		if (err) {
			if (err == USBD_INTERRUPTED)
				error = EINTR;
			else
				error = EIO;
			break;
		}
	}

	return (error);
}

int
uscannerwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uscanner_softc *sc;
	int error;

	sc = devclass_get_softc(uscanner_devclass, USCANNERUNIT(dev));

	sc->sc_refcnt++;
	error = uscanner_do_write(sc, ap->a_uio, ap->a_ioflag);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(sc->sc_dev);
	return (error);
}

static int
uscanner_detach(device_t self)
{
	struct uscanner_softc *sc = device_get_softc(self);

	DPRINTF(("uscanner_detach: sc=%p\n", sc));

	sc->sc_dying = 1;
	sc->sc_dev_flags = 0;	/* make close really close device */

	/* Abort all pipes.  Causes processes waiting for transfer to wake. */
	if (sc->sc_bulkin_pipe != NULL)
		usbd_abort_pipe(sc->sc_bulkin_pipe);
	if (sc->sc_bulkout_pipe != NULL)
		usbd_abort_pipe(sc->sc_bulkout_pipe);

	crit_enter();
	if (--sc->sc_refcnt >= 0) {
		/* Wait for processes to go away. */
		usb_detach_wait(sc->sc_dev);
	}
	crit_exit();

	/* destroy the device for the control endpoint */
	dev_ops_remove_minor(&uscanner_ops, /*-1, */device_get_unit(sc->sc_dev));

	usbd_add_drv_event(USB_EVENT_DRIVER_DETACH, sc->sc_udev,
			   sc->sc_dev);

	return (0);
}

static void
uscannerfilt_detach(struct knote *kn) {}

static int
uscannerfilt(struct knote *kn, long hint)
{
	/*
	 * We have no easy way of determining if a read will
	 * yield any data or a write will happen.
	 * Pretend they will.
	 */
	return (1);
}

static struct filterops uscannerfiltops =
	{ FILTEROP_ISFD, NULL, uscannerfilt_detach, uscannerfilt };

int
uscannerkqfilter(struct dev_kqfilter_args *ap)
{
/* XXX
	cdev_t dev = ap->a_head.a_dev;
	struct uscanner_softc *sc = devclass_get_softc(uscanner_devclass, USCANNERUNIT(dev));

	if (sc->sc_dying)
		return (EIO);
*/

	ap->a_result = 0;
	ap->a_kn->kn_fop = &uscannerfiltops;
	return (0);
}

DRIVER_MODULE(uscanner, uhub, uscanner_driver, uscanner_devclass, usbd_driver_load, NULL);

