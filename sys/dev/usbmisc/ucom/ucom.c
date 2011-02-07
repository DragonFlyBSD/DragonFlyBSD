/*
 * (MPSAFE)
 *
 * $NetBSD: ucom.c,v 1.39 2001/08/16 22:31:24 augustss Exp $
 * $NetBSD: ucom.c,v 1.40 2001/11/13 06:24:54 lukem Exp $
 * $FreeBSD: src/sys/dev/usb/ucom.c,v 1.35 2003/11/16 11:58:21 akiyama Exp $
 */
/*-
 * Copyright (c) 2001-2002, Shunsuke Akiyama <akiyama@jp.FreeBSD.org>.
 * All rights reserved.
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

/*
 * Copyright (c) 1998, 2000 The NetBSD Foundation, Inc.
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
#include <sys/bus.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/clist.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/sysctl.h>
#include <sys/thread2.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbcdc.h>

#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>
#include <bus/usb/usb_quirks.h>

#include "ucomvar.h"

#ifdef USB_DEBUG
static int	ucomdebug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, ucom, CTLFLAG_RW, 0, "USB ucom");
SYSCTL_INT(_hw_usb_ucom, OID_AUTO, debug, CTLFLAG_RW,
	   &ucomdebug, 0, "ucom debug level");
#define DPRINTF(x)	do { \
				if (ucomdebug) \
					kprintf x; \
			} while (0)

#define DPRINTFN(n, x)	do { \
				if (ucomdebug > (n)) \
					kprintf x; \
			} while (0)
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

static d_open_t  ucomopen;
static d_close_t ucomclose;
static d_read_t  ucomread;
static d_write_t ucomwrite;
static d_ioctl_t ucomioctl;

static struct dev_ops ucom_ops = {
	{ "ucom", 0, D_TTY },
	.d_open =	ucomopen,
	.d_close =	ucomclose,
	.d_read =	ucomread,
	.d_write =	ucomwrite,
	.d_ioctl =	ucomioctl,
	.d_kqfilter =	ttykqfilter,
	.d_revoke =	ttyrevoke
};

static void ucom_cleanup(struct ucom_softc *);
static int ucomctl(struct ucom_softc *, int, int);
static int ucomparam(struct tty *, struct termios *);
static void ucomstart(struct tty *);
static void ucomstop(struct tty *, int);
static void ucom_shutdown(struct ucom_softc *);
static void ucom_dtr(struct ucom_softc *, int);
static void ucom_rts(struct ucom_softc *, int);
static void ucom_break(struct ucom_softc *, int);
static usbd_status ucomstartread(struct ucom_softc *);
static void ucomreadcb(usbd_xfer_handle, usbd_private_handle, usbd_status);
static void ucomwritecb(usbd_xfer_handle, usbd_private_handle, usbd_status);
static void ucomstopread(struct ucom_softc *);
static void disc_optim(struct tty *, struct termios *, struct ucom_softc *);

devclass_t ucom_devclass;

static moduledata_t ucom_mod = {
	"ucom",
	NULL,
	NULL
};

DECLARE_MODULE(ucom, ucom_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(ucom, usb, 1, 1, 1);
MODULE_VERSION(ucom, UCOM_MODVER);

int
ucom_attach(struct ucom_softc *sc)
{
	struct tty *tp;
	int unit;
	cdev_t dev;

	lwkt_gettoken(&tty_token);
	unit = device_get_unit(sc->sc_dev);

	sc->sc_tty = tp = ttymalloc(sc->sc_tty);
	tp->t_oproc = ucomstart;
	tp->t_param = ucomparam;
	tp->t_stop = ucomstop;

	DPRINTF(("ucom_attach: tty_attach tp = %p\n", tp));

	DPRINTF(("ucom_attach: make_dev: ucom%d\n", unit));

	dev = make_dev(&ucom_ops, unit | UCOM_CALLOUT_MASK,
			UID_UUCP, GID_DIALER, 0660,
			"ucom%d", unit);
	dev->si_tty = tp;
	sc->dev = dev;
	lwkt_reltoken(&tty_token);

	return (0);
}

int
ucom_detach(struct ucom_softc *sc)
{
	struct tty *tp = sc->sc_tty;
	int unit;

	DPRINTF(("ucom_detach: sc = %p, tp = %p\n", sc, sc->sc_tty));

	destroy_dev(sc->dev);
	lwkt_gettoken(&tty_token);
	sc->sc_dying = 1;

	if (sc->sc_bulkin_pipe != NULL)
		usbd_abort_pipe(sc->sc_bulkin_pipe);
	if (sc->sc_bulkout_pipe != NULL)
		usbd_abort_pipe(sc->sc_bulkout_pipe);

	if (tp != NULL) {
		if (tp->t_state & TS_ISOPEN) {
			device_printf(sc->sc_dev,
				      "still open, forcing close\n");
			(*linesw[tp->t_line].l_close)(tp, 0);
			ttyclose(tp);
		}
	} else {
		DPRINTF(("ucom_detach: no tty\n"));
		lwkt_reltoken(&tty_token);
		return (0);
	}

	crit_enter();
	if (--sc->sc_refcnt >= 0) {
		/* Wait for processes to go away. */
		usb_detach_wait(sc->sc_dev);
	}
	crit_exit();

	unit = device_get_unit(sc->sc_dev);
	dev_ops_remove_minor(&ucom_ops, /*UCOMUNIT_MASK, */unit);

	lwkt_reltoken(&tty_token);
	return (0);
}

static void
ucom_shutdown(struct ucom_softc *sc)
{
	struct tty *tp = sc->sc_tty;

	lwkt_gettoken(&tty_token);
	DPRINTF(("ucom_shutdown\n"));
	/*
	 * Hang up if necessary.  Wait a bit, so the other side has time to
	 * notice even if we immediately open the port again.
	 */
	if (ISSET(tp->t_cflag, HUPCL)) {
		(void)ucomctl(sc, TIOCM_DTR, DMBIC);
		(void)tsleep(sc, 0, "ucomsd", hz);
	}
	lwkt_reltoken(&tty_token);
}

static int
ucomopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int unit = UCOMUNIT(dev);
	struct ucom_softc *sc;
	usbd_status err;
	struct tty *tp;
	int error;

	sc = devclass_get_softc(ucom_devclass, unit);
	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_dying)
		return (ENXIO);

	lwkt_gettoken(&tty_token);
	tp = sc->sc_tty;

	DPRINTF(("%s: ucomopen: tp = %p\n", device_get_nameunit(sc->sc_dev), tp));

	if (ISSET(tp->t_state, TS_ISOPEN) &&
	    ISSET(tp->t_state, TS_XCLUDE) &&
	    priv_check_cred(ap->a_cred, PRIV_ROOT, 0)
	) {
		lwkt_reltoken(&tty_token);
		return (EBUSY);
	}

	/*
	 * Do the following iff this is a first open.
	 */
	crit_enter();
	while (sc->sc_opening)
		tsleep(&sc->sc_opening, 0, "ucomop", 0);
	sc->sc_opening = 1;

	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		struct termios t;

		sc->sc_poll = 0;
		sc->sc_lsr = sc->sc_msr = sc->sc_mcr = 0;

		tp->t_dev = reference_dev(dev);

		/*
		 * Initialize the termios status to the defaults.  Add in the
		 * sticky bits from TIOCSFLAGS.
		 */
		t.c_ispeed = 0;
		t.c_ospeed = TTYDEF_SPEED;
		t.c_cflag = TTYDEF_CFLAG;
		/* Make sure ucomparam() will do something. */
		tp->t_ospeed = 0;
		(void)ucomparam(tp, &t);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		ttychars(tp);
		ttsetwater(tp);

		/*
		 * Turn on DTR.  We must always do this, even if carrier is not
		 * present, because otherwise we'd have to use TIOCSDTR
		 * immediately after setting CLOCAL, which applications do not
		 * expect.  We always assert DTR while the device is open
		 * unless explicitly requested to deassert it.
		 */
		(void)ucomctl(sc, TIOCM_DTR | TIOCM_RTS, DMBIS);

		/* Device specific open */
		if (sc->sc_callback->ucom_open != NULL) {
			error = sc->sc_callback->ucom_open(sc->sc_parent,
							   sc->sc_portno);
			if (error) {
				ucom_cleanup(sc);
				sc->sc_opening = 0;
				wakeup(&sc->sc_opening);
				crit_exit();
				lwkt_reltoken(&tty_token);
				return (error);
			}
		}

		DPRINTF(("ucomopen: open pipes in = %d out = %d\n",
			 sc->sc_bulkin_no, sc->sc_bulkout_no));

		/* Open the bulk pipes */
		/* Bulk-in pipe */
		err = usbd_open_pipe(sc->sc_iface, sc->sc_bulkin_no, 0,
				     &sc->sc_bulkin_pipe);
		if (err) {
			kprintf("%s: open bulk in error (addr %d): %s\n",
			       device_get_nameunit(sc->sc_dev), sc->sc_bulkin_no,
			       usbd_errstr(err));
			error = EIO;
			goto fail_0;
		}
		/* Bulk-out pipe */
		err = usbd_open_pipe(sc->sc_iface, sc->sc_bulkout_no,
				     USBD_EXCLUSIVE_USE, &sc->sc_bulkout_pipe);
		if (err) {
			kprintf("%s: open bulk out error (addr %d): %s\n",
			       device_get_nameunit(sc->sc_dev), sc->sc_bulkout_no,
			       usbd_errstr(err));
			error = EIO;
			goto fail_1;
		}

		/* Allocate a request and an input buffer and start reading. */
		sc->sc_ixfer = usbd_alloc_xfer(sc->sc_udev);
		if (sc->sc_ixfer == NULL) {
			error = ENOMEM;
			goto fail_2;
		}

		sc->sc_ibuf = usbd_alloc_buffer(sc->sc_ixfer,
						sc->sc_ibufsizepad);
		if (sc->sc_ibuf == NULL) {
			error = ENOMEM;
			goto fail_3;
		}

		sc->sc_oxfer = usbd_alloc_xfer(sc->sc_udev);
		if (sc->sc_oxfer == NULL) {
			error = ENOMEM;
			goto fail_3;
		}

		sc->sc_obuf = usbd_alloc_buffer(sc->sc_oxfer,
						sc->sc_obufsize +
						sc->sc_opkthdrlen);
		if (sc->sc_obuf == NULL) {
			error = ENOMEM;
			goto fail_4;
		}

		/*
		 * Handle initial DCD.
		 */
		if (ISSET(sc->sc_msr, UMSR_DCD) ||
		    (minor(dev) & UCOM_CALLOUT_MASK))
			(*linesw[tp->t_line].l_modem)(tp, 1);

		ucomstartread(sc);
	}

	sc->sc_opening = 0;
	wakeup(&sc->sc_opening);
	crit_exit();

	error = ttyopen(dev, tp);
	if (error)
		goto bad;

	error = (*linesw[tp->t_line].l_open)(dev, tp);
	if (error)
		goto bad;

	disc_optim(tp, &tp->t_termios, sc);

	DPRINTF(("%s: ucomopen: success\n", device_get_nameunit(sc->sc_dev)));

	sc->sc_poll = 1;
	sc->sc_refcnt++;

	lwkt_reltoken(&tty_token);
	return (0);

fail_4:
	usbd_free_xfer(sc->sc_oxfer);
	sc->sc_oxfer = NULL;
fail_3:
	usbd_free_xfer(sc->sc_ixfer);
	sc->sc_ixfer = NULL;
fail_2:
	usbd_close_pipe(sc->sc_bulkout_pipe);
	sc->sc_bulkout_pipe = NULL;
fail_1:
	usbd_close_pipe(sc->sc_bulkin_pipe);
	sc->sc_bulkin_pipe = NULL;
fail_0:
	sc->sc_opening = 0;
	wakeup(&sc->sc_opening);
	crit_exit();
	lwkt_reltoken(&tty_token);
	return (error);

bad:
	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		/*
		 * We failed to open the device, and nobody else had it opened.
		 * Clean up the state as appropriate.
		 */
		ucom_cleanup(sc);
	}

	DPRINTF(("%s: ucomopen: failed\n", device_get_nameunit(sc->sc_dev)));

	lwkt_reltoken(&tty_token);
	return (error);
}

static int
ucomclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ucom_softc *sc;
	struct tty *tp;

	sc = devclass_get_softc(ucom_devclass, UCOMUNIT(dev));

	lwkt_gettoken(&tty_token);
	tp = sc->sc_tty;

	DPRINTF(("%s: ucomclose: unit = %d\n",
		device_get_nameunit(sc->sc_dev), UCOMUNIT(dev)));

	if (!ISSET(tp->t_state, TS_ISOPEN))
		goto quit;

	crit_enter();
	(*linesw[tp->t_line].l_close)(tp, ap->a_fflag);
	disc_optim(tp, &tp->t_termios, sc);
	ttyclose(tp);
	crit_exit();

	if (sc->sc_dying)
		goto quit;

	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		/*
		 * Although we got a last close, the device may still be in
		 * use; e.g. if this was the dialout node, and there are still
		 * processes waiting for carrier on the non-dialout node.
		 */
		ucom_cleanup(sc);
	}

	if (sc->sc_callback->ucom_close != NULL)
		sc->sc_callback->ucom_close(sc->sc_parent, sc->sc_portno);

quit:
	if (tp->t_dev) {
		release_dev(tp->t_dev);
		tp->t_dev = NULL;
	}

	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(sc->sc_dev);

	lwkt_reltoken(&tty_token);
	return (0);
}

static int
ucomread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ucom_softc *sc;
	struct tty *tp;
	int error;

	sc = devclass_get_softc(ucom_devclass, UCOMUNIT(dev));
	lwkt_gettoken(&tty_token);
	tp = sc->sc_tty;

	DPRINTF(("ucomread: tp = %p, flag = 0x%x\n", tp, ap->a_ioflag));

	if (sc->sc_dying) {
		lwkt_reltoken(&tty_token);
		return (EIO);
	}

	error = (*linesw[tp->t_line].l_read)(tp, ap->a_uio, ap->a_ioflag);

	DPRINTF(("ucomread: error = %d\n", error));

	lwkt_reltoken(&tty_token);
	return (error);
}

static int
ucomwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ucom_softc *sc;
	struct tty *tp;
	int error;

	sc = devclass_get_softc(ucom_devclass, UCOMUNIT(dev));
	lwkt_gettoken(&tty_token);
	tp = sc->sc_tty;

	DPRINTF(("ucomwrite: tp = %p, flag = 0x%x\n", tp, ap->a_ioflag));

	if (sc->sc_dying) {
		lwkt_reltoken(&tty_token);
		return (EIO);
	}

	error = (*linesw[tp->t_line].l_write)(tp, ap->a_uio, ap->a_ioflag);

	DPRINTF(("ucomwrite: error = %d\n", error));

	lwkt_reltoken(&tty_token);
	return (error);
}

static int
ucomioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ucom_softc *sc;
	struct tty *tp;
	int error;
	int d;

	sc = devclass_get_softc(ucom_devclass, UCOMUNIT(dev));
	lwkt_gettoken(&tty_token);
	tp = sc->sc_tty;

	if (sc->sc_dying) {
		lwkt_reltoken(&tty_token);
		return (EIO);
	}

	DPRINTF(("ucomioctl: cmd = 0x%08lx\n", ap->a_cmd));

	error = (*linesw[tp->t_line].l_ioctl)(tp, ap->a_cmd, ap->a_data,
					      ap->a_fflag, ap->a_cred);
	if (error != ENOIOCTL) {
		DPRINTF(("ucomioctl: l_ioctl: error = %d\n", error));
		lwkt_reltoken(&tty_token);
		return (error);
	}

	crit_enter();

	error = ttioctl(tp, ap->a_cmd, ap->a_data, ap->a_fflag);
	disc_optim(tp, &tp->t_termios, sc);
	if (error != ENOIOCTL) {
		crit_exit();
		DPRINTF(("ucomioctl: ttioctl: error = %d\n", error));
		lwkt_reltoken(&tty_token);
		return (error);
	}

	if (sc->sc_callback->ucom_ioctl != NULL) {
		error = sc->sc_callback->ucom_ioctl(sc->sc_parent,
						    sc->sc_portno,
						    ap->a_cmd, ap->a_data,
						    ap->a_fflag, curthread);
		if (error >= 0) {
			crit_exit();
			lwkt_reltoken(&tty_token);
			return (error);
		}
	}

	error = 0;

	DPRINTF(("ucomioctl: our cmd = 0x%08lx\n", ap->a_cmd));

	switch (ap->a_cmd) {
	case TIOCSBRK:
		DPRINTF(("ucomioctl: TIOCSBRK\n"));
		ucom_break(sc, 1);
		break;
	case TIOCCBRK:
		DPRINTF(("ucomioctl: TIOCCBRK\n"));
		ucom_break(sc, 0);
		break;

	case TIOCSDTR:
		DPRINTF(("ucomioctl: TIOCSDTR\n"));
		(void)ucomctl(sc, TIOCM_DTR, DMBIS);
		break;
	case TIOCCDTR:
		DPRINTF(("ucomioctl: TIOCCDTR\n"));
		(void)ucomctl(sc, TIOCM_DTR, DMBIC);
		break;

	case TIOCMSET:
		d = *(int *)ap->a_data;
		DPRINTF(("ucomioctl: TIOCMSET, 0x%x\n", d));
		(void)ucomctl(sc, d, DMSET);
		break;
	case TIOCMBIS:
		d = *(int *)ap->a_data;
		DPRINTF(("ucomioctl: TIOCMBIS, 0x%x\n", d));
		(void)ucomctl(sc, d, DMBIS);
		break;
	case TIOCMBIC:
		d = *(int *)ap->a_data;
		DPRINTF(("ucomioctl: TIOCMBIC, 0x%x\n", d));
		(void)ucomctl(sc, d, DMBIC);
		break;
	case TIOCMGET:
		d = ucomctl(sc, 0, DMGET);
		DPRINTF(("ucomioctl: TIOCMGET, 0x%x\n", d));
		*(int *)ap->a_data = d;
		break;

	default:
		DPRINTF(("ucomioctl: error: our cmd = 0x%08lx\n", ap->a_cmd));
		error = ENOTTY;
		break;
	}

	crit_exit();

	lwkt_reltoken(&tty_token);
	return (error);
}

/*
 * NOTE: Must be called with tty_token held.
 */
static int
ucomctl(struct ucom_softc *sc, int bits, int how)
{
	int	mcr;
	int	msr;
	int	onoff;

	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	DPRINTF(("ucomctl: bits = 0x%x, how = %d\n", bits, how));

	if (how == DMGET) {
		SET(bits, TIOCM_LE);		/* always set TIOCM_LE bit */
		DPRINTF(("ucomctl: DMGET: LE"));

		mcr = sc->sc_mcr;
		if (ISSET(mcr, UMCR_DTR)) {
			SET(bits, TIOCM_DTR);
			DPRINTF((" DTR"));
		}
		if (ISSET(mcr, UMCR_RTS)) {
			SET(bits, TIOCM_RTS);
			DPRINTF((" RTS"));
		}

		msr = sc->sc_msr;
		if (ISSET(msr, UMSR_CTS)) {
			SET(bits, TIOCM_CTS);
			DPRINTF((" CTS"));
		}
		if (ISSET(msr, UMSR_DCD)) {
			SET(bits, TIOCM_CD);
			DPRINTF((" CD"));
		}
		if (ISSET(msr, UMSR_DSR)) {
			SET(bits, TIOCM_DSR);
			DPRINTF((" DSR"));
		}
		if (ISSET(msr, UMSR_RI)) {
			SET(bits, TIOCM_RI);
			DPRINTF((" RI"));
		}

		DPRINTF(("\n"));

		return (bits);
	}

	mcr = 0;
	if (ISSET(bits, TIOCM_DTR))
		SET(mcr, UMCR_DTR);
	if (ISSET(bits, TIOCM_RTS))
		SET(mcr, UMCR_RTS);

	switch (how) {
	case DMSET:
		sc->sc_mcr = mcr;
		break;
	case DMBIS:
		sc->sc_mcr |= mcr;
		break;
	case DMBIC:
		sc->sc_mcr &= ~mcr;
		break;
	}

	onoff = ISSET(sc->sc_mcr, UMCR_DTR) ? 1 : 0;
	ucom_dtr(sc, onoff);

	onoff = ISSET(sc->sc_mcr, UMCR_RTS) ? 1 : 0;
	ucom_rts(sc, onoff);

	return (0);
}

/*
 * NOTE: Must be called with tty_token held.
 */
static void
ucom_break(struct ucom_softc *sc, int onoff)
{
	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	DPRINTF(("ucom_break: onoff = %d\n", onoff));

	if (sc->sc_callback->ucom_set == NULL)
		return;
	sc->sc_callback->ucom_set(sc->sc_parent, sc->sc_portno,
				  UCOM_SET_BREAK, onoff);
}

/*
 * NOTE: Must be called with tty_token held.
 */
static void
ucom_dtr(struct ucom_softc *sc, int onoff)
{
	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	DPRINTF(("ucom_dtr: onoff = %d\n", onoff));

	if (sc->sc_callback->ucom_set == NULL)
		return;
	sc->sc_callback->ucom_set(sc->sc_parent, sc->sc_portno,
				  UCOM_SET_DTR, onoff);
}

/*
 * NOTE: Must be called with tty_token held.
 */
static void
ucom_rts(struct ucom_softc *sc, int onoff)
{
	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	DPRINTF(("ucom_rts: onoff = %d\n", onoff));

	if (sc->sc_callback->ucom_set == NULL)
		return;
	sc->sc_callback->ucom_set(sc->sc_parent, sc->sc_portno,
				  UCOM_SET_RTS, onoff);
}

void
ucom_status_change(struct ucom_softc *sc)
{
	struct tty *tp = sc->sc_tty;
	u_char old_msr;
	int onoff;

	lwkt_gettoken(&tty_token);
	if (sc->sc_callback->ucom_get_status == NULL) {
		sc->sc_lsr = 0;
		sc->sc_msr = 0;
		lwkt_reltoken(&tty_token);
		return;
	}

	old_msr = sc->sc_msr;
	sc->sc_callback->ucom_get_status(sc->sc_parent, sc->sc_portno,
					 &sc->sc_lsr, &sc->sc_msr);
	if (ISSET((sc->sc_msr ^ old_msr), UMSR_DCD)) {
		if (sc->sc_poll == 0) {
			lwkt_reltoken(&tty_token);
			return;
		}
		onoff = ISSET(sc->sc_msr, UMSR_DCD) ? 1 : 0;
		DPRINTF(("ucom_status_change: DCD changed to %d\n", onoff));
		(*linesw[tp->t_line].l_modem)(tp, onoff);
	}
	lwkt_reltoken(&tty_token);
}

static int
ucomparam(struct tty *tp, struct termios *t)
{
	struct ucom_softc *sc;
	int error;
	usbd_status uerr;

	sc = devclass_get_softc(ucom_devclass, UCOMUNIT(tp->t_dev));

	lwkt_gettoken(&tty_token);
	if (sc->sc_dying) {
		lwkt_reltoken(&tty_token);
		return (EIO);
	}

	DPRINTF(("ucomparam: sc = %p\n", sc));

	/* Check requested parameters. */
	if (t->c_ospeed < 0) {
		DPRINTF(("ucomparam: negative ospeed\n"));
		lwkt_reltoken(&tty_token);
		return (EINVAL);
	}
	if (t->c_ispeed && t->c_ispeed != t->c_ospeed) {
		DPRINTF(("ucomparam: mismatch ispeed and ospeed\n"));
		lwkt_reltoken(&tty_token);
		return (EINVAL);
	}

	/*
	 * If there were no changes, don't do anything.  This avoids dropping
	 * input and improves performance when all we did was frob things like
	 * VMIN and VTIME.
	 */
	if (tp->t_ospeed == t->c_ospeed &&
	    tp->t_cflag == t->c_cflag) {
		lwkt_reltoken(&tty_token);
		return (0);
	}

	/* And copy to tty. */
	tp->t_ispeed = 0;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;

	if (sc->sc_callback->ucom_param == NULL) {
		lwkt_reltoken(&tty_token);
		return (0);
	}

	ucomstopread(sc);

	error = sc->sc_callback->ucom_param(sc->sc_parent, sc->sc_portno, t);
	if (error) {
		DPRINTF(("ucomparam: callback: error = %d\n", error));
		lwkt_reltoken(&tty_token);
		return (error);
	}

	ttsetwater(tp);

	if (t->c_cflag & CRTS_IFLOW) {
		sc->sc_state |= UCS_RTS_IFLOW;
	} else if (sc->sc_state & UCS_RTS_IFLOW) {
		sc->sc_state &= ~UCS_RTS_IFLOW;
		(void)ucomctl(sc, UMCR_RTS, DMBIS);
	}

	disc_optim(tp, t, sc);

	uerr = ucomstartread(sc);
	if (uerr != USBD_NORMAL_COMPLETION) {
		lwkt_reltoken(&tty_token);
		return (EIO);
	}

	lwkt_reltoken(&tty_token);
	return (0);
}

static void
ucomstart(struct tty *tp)
{
	struct ucom_softc *sc;
	struct cblock *cbp;
	usbd_status err;
	u_char *data;
	int cnt;

	sc = devclass_get_softc(ucom_devclass, UCOMUNIT(tp->t_dev));
	DPRINTF(("ucomstart: sc = %p\n", sc));

	lwkt_gettoken(&tty_token);

	if (sc->sc_dying) {
		lwkt_reltoken(&tty_token);
		return;
	}

	crit_enter();

	if (tp->t_state & TS_TBLOCK) {
		if (ISSET(sc->sc_mcr, UMCR_RTS) &&
		    ISSET(sc->sc_state, UCS_RTS_IFLOW)) {
			DPRINTF(("ucomstart: clear RTS\n"));
			(void)ucomctl(sc, UMCR_RTS, DMBIC);
		}
	} else {
		if (!ISSET(sc->sc_mcr, UMCR_RTS) &&
		    tp->t_rawq.c_cc <= tp->t_ilowat &&
		    ISSET(sc->sc_state, UCS_RTS_IFLOW)) {
			DPRINTF(("ucomstart: set RTS\n"));
			(void)ucomctl(sc, UMCR_RTS, DMBIS);
		}
	}

	if (ISSET(tp->t_state, TS_BUSY | TS_TIMEOUT | TS_TTSTOP)) {
		ttwwakeup(tp);
		DPRINTF(("ucomstart: stopped\n"));
		goto out;
	}

	if (tp->t_outq.c_cc <= tp->t_olowat) {
		if (ISSET(tp->t_state, TS_SO_OLOWAT)) {
			CLR(tp->t_state, TS_SO_OLOWAT);
			wakeup(TSA_OLOWAT(tp));
		}
		KNOTE(&tp->t_wkq.ki_note, 0);
		if (tp->t_outq.c_cc == 0) {
			if (ISSET(tp->t_state, TS_BUSY | TS_SO_OCOMPLETE) ==
			    TS_SO_OCOMPLETE && tp->t_outq.c_cc == 0) {
				CLR(tp->t_state, TS_SO_OCOMPLETE);
				wakeup(TSA_OCOMPLETE(tp));
			}
			goto out;
		}
	}

	/* Grab the first contiguous region of buffer space. */
	data = tp->t_outq.c_cf;
	cbp = (struct cblock *) ((intptr_t) tp->t_outq.c_cf & ~CROUND);
	cnt = min((char *) (cbp+1) - tp->t_outq.c_cf, tp->t_outq.c_cc);

	if (cnt == 0) {
		DPRINTF(("ucomstart: cnt == 0\n"));
		goto out;
	}

	SET(tp->t_state, TS_BUSY);

	if (cnt > sc->sc_obufsize) {
		DPRINTF(("ucomstart: big buffer %d chars\n", cnt));
		cnt = sc->sc_obufsize;
	}
	if (sc->sc_callback->ucom_write != NULL)
		sc->sc_callback->ucom_write(sc->sc_parent, sc->sc_portno,
					    sc->sc_obuf, data, &cnt);
	else
		memcpy(sc->sc_obuf, data, cnt);

	DPRINTF(("ucomstart: %d chars\n", cnt));
	usbd_setup_xfer(sc->sc_oxfer, sc->sc_bulkout_pipe,
			(usbd_private_handle)sc, sc->sc_obuf, cnt,
			USBD_NO_COPY, USBD_NO_TIMEOUT, ucomwritecb);
	/* What can we do on error? */
	err = usbd_transfer(sc->sc_oxfer);
	if (err != USBD_IN_PROGRESS)
		kprintf("ucomstart: err=%s\n", usbd_errstr(err));

	ttwwakeup(tp);

    out:
	crit_exit();
	lwkt_reltoken(&tty_token);
}

static void
ucomstop(struct tty *tp, int flag)
{
	struct ucom_softc *sc;

	sc = devclass_get_softc(ucom_devclass, UCOMUNIT(tp->t_dev));

	DPRINTF(("ucomstop: %d\n", flag));

	lwkt_gettoken(&tty_token);
	if (flag & FREAD) {
		/*
		 * This is just supposed to flush pending receive data,
		 * not stop the reception of data entirely!
		 */
		DPRINTF(("ucomstop: read\n"));
		ucomstopread(sc);
		ucomstartread(sc);
	}

	if (flag & FWRITE) {
		DPRINTF(("ucomstop: write\n"));
		crit_enter();
		if (ISSET(tp->t_state, TS_BUSY)) {
			/* XXX do what? */
			if (!ISSET(tp->t_state, TS_TTSTOP))
				SET(tp->t_state, TS_FLUSH);
		}
		crit_exit();
	}

	DPRINTF(("ucomstop: done\n"));
	lwkt_reltoken(&tty_token);
}

static void
ucomwritecb(usbd_xfer_handle xfer, usbd_private_handle p, usbd_status status)
{
	struct ucom_softc *sc = (struct ucom_softc *)p;
	struct tty *tp = sc->sc_tty;
	u_int32_t cc;

	lwkt_gettoken(&tty_token);
	DPRINTF(("ucomwritecb: status = %d\n", status));

	if (status == USBD_CANCELLED || sc->sc_dying)
		goto error;

	if (status != USBD_NORMAL_COMPLETION) {
		kprintf("%s: ucomwritecb: %s\n",
		       device_get_nameunit(sc->sc_dev), usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_bulkin_pipe);
		/*
		 * XXX.  We may need a flag to sequence ucomstopread() and
		 * ucomstartread() to handle the case where ucomstartread()
		 * is called after ucomstopread() but before the request has
		 * been properly canceled?
		 */
		goto error;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &cc, NULL);
	DPRINTF(("ucomwritecb: cc = %d\n", cc));
	if (cc <= sc->sc_opkthdrlen) {
		kprintf("%s: sent size too small, cc = %d\n",
			device_get_nameunit(sc->sc_dev), cc);
		goto error;
	}

	/* convert from USB bytes to tty bytes */
	cc -= sc->sc_opkthdrlen;

	crit_enter();
	CLR(tp->t_state, TS_BUSY);
	if (ISSET(tp->t_state, TS_FLUSH))
		CLR(tp->t_state, TS_FLUSH);
	else
		ndflush(&tp->t_outq, cc);
	(*linesw[tp->t_line].l_start)(tp);
	crit_exit();

	lwkt_reltoken(&tty_token);
	return;

  error:
	crit_enter();
	CLR(tp->t_state, TS_BUSY);
	crit_exit();
	lwkt_reltoken(&tty_token);
	return;
}

/*
 * NOTE: Must be called with tty_token held
 */
static usbd_status
ucomstartread(struct ucom_softc *sc)
{
	usbd_status err;

	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	DPRINTF(("ucomstartread: start\n"));

	sc->sc_state &= ~UCS_RXSTOP;

	if (sc->sc_bulkin_pipe == NULL)
		return (USBD_NORMAL_COMPLETION);

	usbd_setup_xfer(sc->sc_ixfer, sc->sc_bulkin_pipe,
			(usbd_private_handle)sc,
			sc->sc_ibuf, sc->sc_ibufsize,
			USBD_SHORT_XFER_OK | USBD_NO_COPY,
			USBD_NO_TIMEOUT, ucomreadcb);

	err = usbd_transfer(sc->sc_ixfer);
	if (err != USBD_IN_PROGRESS) {
		DPRINTF(("ucomstartread: err = %s\n", usbd_errstr(err)));
		return (err);
	}

	return (USBD_NORMAL_COMPLETION);
}

static void
ucomreadcb(usbd_xfer_handle xfer, usbd_private_handle p, usbd_status status)
{
	lwkt_gettoken(&tty_token);

	struct ucom_softc *sc = (struct ucom_softc *)p;
	struct tty *tp = sc->sc_tty;
	int (*rint) (int c, struct tty *tp) = linesw[tp->t_line].l_rint;
	usbd_status err;
	u_int32_t cc;
	u_char *cp;
	int lostcc;

	DPRINTF(("ucomreadcb: status = %d\n", status));

	if (status != USBD_NORMAL_COMPLETION) {
		if (!(sc->sc_state & UCS_RXSTOP))
			kprintf("%s: ucomreadcb: %s\n",
			       device_get_nameunit(sc->sc_dev), usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_bulkin_pipe);
		/* XXX we should restart after some delay. */
		lwkt_reltoken(&tty_token);
		return;
	}

	usbd_get_xfer_status(xfer, NULL, (void **)&cp, &cc, NULL);
	DPRINTF(("ucomreadcb: got %d chars, tp = %p\n", cc, tp));
	if (cc == 0)
		goto resubmit;

	if (sc->sc_callback->ucom_read != NULL) {
		sc->sc_callback->ucom_read(sc->sc_parent, sc->sc_portno,
					   &cp, &cc);
	}

	if (cc > sc->sc_ibufsize) {
		kprintf("%s: invalid receive data size, %d chars\n",
			device_get_nameunit(sc->sc_dev), cc);
		goto resubmit;
	}
	if (cc < 1)
		goto resubmit;

	crit_enter();
	if (tp->t_state & TS_CAN_BYPASS_L_RINT) {
		if (tp->t_rawq.c_cc + cc > tp->t_ihiwat
		    && (sc->sc_state & UCS_RTS_IFLOW
			|| tp->t_iflag & IXOFF)
		    && !(tp->t_state & TS_TBLOCK))
			ttyblock(tp);
		lostcc = b_to_q((char *)cp, cc, &tp->t_rawq);
		tp->t_rawcc += cc;
		if (sc->hotchar) {
			while (cc) {
				if (*cp == sc->hotchar)
					break;
				--cc;
				++cp;
			}
			if (cc)
				setsofttty();
		}
		ttwakeup(tp);
		if (tp->t_state & TS_TTSTOP
		    && (tp->t_iflag & IXANY
			|| tp->t_cc[VSTART] == tp->t_cc[VSTOP])) {
			tp->t_state &= ~TS_TTSTOP;
			tp->t_lflag &= ~FLUSHO;
			ucomstart(tp);
		}
		if (lostcc > 0)
			kprintf("%s: lost %d chars\n", device_get_nameunit(sc->sc_dev),
			       lostcc);
	} else {
		/* Give characters to tty layer. */
		while (cc > 0) {
			DPRINTFN(7, ("ucomreadcb: char = 0x%02x\n", *cp));
			if ((*rint)(*cp, tp) == -1) {
				/* XXX what should we do? */
				kprintf("%s: lost %d chars\n",
				       device_get_nameunit(sc->sc_dev), cc);
				break;
			}
			cc--;
			cp++;
		}
	}
	crit_exit();

resubmit:
	err = ucomstartread(sc);
	if (err) {
		kprintf("%s: read start failed\n", device_get_nameunit(sc->sc_dev));
		/* XXX what should we dow now? */
	}

	if ((sc->sc_state & UCS_RTS_IFLOW) && !ISSET(sc->sc_mcr, UMCR_RTS)
	    && !(tp->t_state & TS_TBLOCK))
		ucomctl(sc, UMCR_RTS, DMBIS);

	lwkt_reltoken(&tty_token);
}

/*
 * NOTE: Must be called with tty_token held.
 */
static void
ucom_cleanup(struct ucom_softc *sc)
{
	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	DPRINTF(("ucom_cleanup: closing pipes\n"));

	ucom_shutdown(sc);
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
	if (sc->sc_ixfer != NULL) {
		usbd_free_xfer(sc->sc_ixfer);
		sc->sc_ixfer = NULL;
	}
	if (sc->sc_oxfer != NULL) {
		usbd_free_xfer(sc->sc_oxfer);
		sc->sc_oxfer = NULL;
	}
}

/*
 * NOTE: Must be called with tty_token held.
 */
static void
ucomstopread(struct ucom_softc *sc)
{
	usbd_status err;

	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	DPRINTF(("ucomstopread: enter\n"));

	if (!(sc->sc_state & UCS_RXSTOP)) {
		sc->sc_state |= UCS_RXSTOP;
		if (sc->sc_bulkin_pipe == NULL) {
			DPRINTF(("ucomstopread: bulkin pipe NULL\n"));
			return;
		}
		err = usbd_abort_pipe(sc->sc_bulkin_pipe);
		if (err) {
			DPRINTF(("ucomstopread: err = %s\n",
				 usbd_errstr(err)));
		}
	}

	DPRINTF(("ucomstopread: leave\n"));
}

/*
 * NOTE: Must be called with tty_token held.
 */
static void
disc_optim(struct tty *tp, struct termios *t, struct ucom_softc *sc)
{
	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	if (!(t->c_iflag & (ICRNL | IGNCR | IMAXBEL | INLCR | ISTRIP | IXON))
	    && (!(t->c_iflag & BRKINT) || (t->c_iflag & IGNBRK))
	    && (!(t->c_iflag & PARMRK)
		|| (t->c_iflag & (IGNPAR | IGNBRK)) == (IGNPAR | IGNBRK))
	    && !(t->c_lflag & (ECHO | ICANON | IEXTEN | ISIG | PENDIN))
	    && linesw[tp->t_line].l_rint == ttyinput) {
		DPRINTF(("disc_optim: bypass l_rint\n"));
		tp->t_state |= TS_CAN_BYPASS_L_RINT;
	} else {
		DPRINTF(("disc_optim: can't bypass l_rint\n"));
		tp->t_state &= ~TS_CAN_BYPASS_L_RINT;
	}
	sc->hotchar = linesw[tp->t_line].l_hotchar;
}
