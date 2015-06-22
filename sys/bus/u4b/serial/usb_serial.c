/*	$NetBSD: ucom.c,v 1.40 2001/11/13 06:24:54 lukem Exp $	*/

/*-
 * Copyright (c) 2001-2003, 2005, 2008
 *	Shunsuke Akiyama <akiyama@jp.FreeBSD.org>.
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

/*-
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

/*
 * XXX profmakx
 * This is a Frankenstein of FreeBSD's usb4bsd ucom and Dragonfly's old ucom
 * module. There might be bugs lurking everywhere still
 *
 * In particular serial console on ucom is completely untested and likely broken
 * as well as anyting that requires the modem control lines.
 */

#include <sys/stdint.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/cons.h>
#include <sys/serial.h>
#include <sys/thread2.h>
#include <sys/conf.h>
#include <sys/clist.h>
 
#include <bus/u4b/usb.h>
#include <bus/u4b/usbdi.h>
#include <bus/u4b/usbdi_util.h>

#define	USB_DEBUG_VAR ucom_debug
#include <bus/u4b/usb_debug.h>
#include <bus/u4b/usb_busdma.h>
#include <bus/u4b/usb_process.h>

#include <bus/u4b/serial/usb_serial.h>

//#include "opt_gdb.h"

static SYSCTL_NODE(_hw_usb, OID_AUTO, ucom, CTLFLAG_RW, 0, "USB ucom");

static int ucom_pps_mode;

SYSCTL_INT(_hw_usb_ucom, OID_AUTO, pps_mode, CTLFLAG_RW,
    &ucom_pps_mode, 0, "pulse capturing mode - 0/1/2 - disabled/CTS/DCD");
TUNABLE_INT("hw.usb.ucom.pss_mode", &ucom_pps_mode);


#ifdef USB_DEBUG
static int ucom_debug = 0;

SYSCTL_INT(_hw_usb_ucom, OID_AUTO, debug, CTLFLAG_RW,
    &ucom_debug, 0, "ucom debug level");
#endif

#define	UCOM_CONS_BUFSIZE 1024

static uint8_t ucom_cons_rx_buf[UCOM_CONS_BUFSIZE];
static uint8_t ucom_cons_tx_buf[UCOM_CONS_BUFSIZE];

static unsigned int ucom_cons_rx_low = 0;
static unsigned int ucom_cons_rx_high = 0;

static unsigned int ucom_cons_tx_low = 0;
static unsigned int ucom_cons_tx_high = 0;

static int ucom_cons_unit = -1;
static int ucom_cons_subunit = 0;
static int ucom_cons_baud = 9600;
static struct ucom_softc *ucom_cons_softc = NULL;

TUNABLE_INT("hw.usb.ucom.cons_unit", &ucom_cons_unit);
SYSCTL_INT(_hw_usb_ucom, OID_AUTO, cons_unit, CTLFLAG_RW,
    &ucom_cons_unit, 0, "console unit number");
TUNABLE_INT("hw.usb.ucom.cons_subunit", &ucom_cons_subunit);
SYSCTL_INT(_hw_usb_ucom, OID_AUTO, cons_subunit, CTLFLAG_RW,
    &ucom_cons_subunit, 0, "console subunit number");
TUNABLE_INT("hw.usb.ucom.cons_baud", &ucom_cons_baud);
SYSCTL_INT(_hw_usb_ucom, OID_AUTO, cons_baud, CTLFLAG_RW,
    &ucom_cons_baud, 0, "console baud rate");

static usb_proc_callback_t ucom_cfg_start_transfers;
static usb_proc_callback_t ucom_cfg_open;
static usb_proc_callback_t ucom_cfg_close;
static usb_proc_callback_t ucom_cfg_line_state;
static usb_proc_callback_t ucom_cfg_status_change;
static usb_proc_callback_t ucom_cfg_param;

static int	ucom_unit_alloc(void);
static void	ucom_unit_free(int);
static int	ucom_attach_tty(struct ucom_super_softc *, struct ucom_softc *);
static void	ucom_detach_tty(struct ucom_super_softc *, struct ucom_softc *);
static void	ucom_queue_command(struct ucom_softc *,
		    usb_proc_callback_t *, struct termios *pt,
		    struct usb_proc_msg *t0, struct usb_proc_msg *t1);
static void	ucom_shutdown(struct ucom_softc *);
static void	ucom_ring(struct ucom_softc *, uint8_t);
static void	ucom_break(struct ucom_softc *, uint8_t);
static void	ucom_dtr(struct ucom_softc *, uint8_t);
static void	ucom_rts(struct ucom_softc *, uint8_t);

static int ucom_open(struct ucom_softc *sc);
static int ucom_close(struct ucom_softc *sc);
static void ucom_start(struct tty *tp);
static void ucom_stop(struct tty *tp, int);
static int ucom_param(struct tty *tp, struct termios *t);
static int ucom_modem(struct tty *tp, int sigon, int sigoff);

static int ucom_fromtio(int);
static int ucom_totio(int);

static void disc_optim(struct tty *, struct termios *, struct ucom_softc *);

static d_open_t ucom_dev_open;
static d_close_t ucom_dev_close;
static d_read_t ucom_dev_read;
static d_write_t ucom_dev_write;
static d_ioctl_t ucom_dev_ioctl;

static struct dev_ops ucom_ops = {
  { "ucom", 0, D_MPSAFE | D_TTY },
  .d_open =       ucom_dev_open,
  .d_close =      ucom_dev_close,
  .d_read =       ucom_dev_read,
  .d_write =      ucom_dev_write,
  .d_ioctl =      ucom_dev_ioctl,
  .d_kqfilter =   ttykqfilter,
  .d_revoke =     ttyrevoke
};

static moduledata_t ucom_mod = {
        "ucom",
        NULL,
        NULL
};

DECLARE_MODULE(ucom, ucom_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(ucom, usb, 1, 1, 1);
MODULE_VERSION(ucom, UCOM_MODVER);

/* XXXDF */
#define tty_gone(tp) ((tp->t_state) & (TS_ZOMBIE))

#define	UCOM_UNIT_MAX 		128	/* maximum number of units */
#define	UCOM_TTY_PREFIX		"ucom"


#define CALLOUT_MASK            0x80
#define CONTROL_MASK            0x60
#define CONTROL_INIT_STATE      0x20
#define CONTROL_LOCK_STATE      0x40

static struct unrhdr *ucom_unrhdr;
static struct lock ucom_lock;
static int ucom_close_refs;

static void
ucom_init(void *arg)
{
	DPRINTF("\n");
	ucom_unrhdr = new_unrhdr(0, UCOM_UNIT_MAX - 1, NULL);
	lockinit(&ucom_lock, "UCOM LOCK", 0, 0);
}
SYSINIT(ucom_init, SI_BOOT2_KLD - 1, SI_ORDER_ANY, ucom_init, NULL);

static void
ucom_uninit(void *arg)
{
	struct unrhdr *hdr;
	hdr = ucom_unrhdr;
	ucom_unrhdr = NULL;

	DPRINTF("\n");

	if (hdr != NULL)
		delete_unrhdr(hdr);

	lockuninit(&ucom_lock);
}
SYSUNINIT(ucom_uninit, SI_BOOT2_KLD - 2, SI_ORDER_ANY, ucom_uninit, NULL);

/*
 * Mark a unit number (the X in cuaUX) as in use.
 *
 * Note that devices using a different naming scheme (see ucom_tty_name()
 * callback) still use this unit allocation.
 */
static int
ucom_unit_alloc(void)
{
	int unit;

	/* sanity checks */
	if (ucom_unrhdr == NULL) {
		DPRINTF("ucom_unrhdr is NULL\n");
		return (-1);
	}
	unit = alloc_unr(ucom_unrhdr);
	DPRINTF("unit %d is allocated\n", unit);
	return (unit);
}

/*
 * Mark the unit number as not in use.
 */
static void
ucom_unit_free(int unit)
{
	/* sanity checks */
	if (unit < 0 || unit >= UCOM_UNIT_MAX || ucom_unrhdr == NULL) {
		DPRINTF("cannot free unit number\n");
		return;
	}
	DPRINTF("unit %d is freed\n", unit);
	free_unr(ucom_unrhdr, unit);
}

/*
 * Setup a group of one or more serial ports.
 *
 * The lock pointed to by "lock" is applied before all
 * callbacks are called back. Also "lock" must be applied
 * before calling into the ucom-layer!
 */
int
ucom_attach(struct ucom_super_softc *ssc, struct ucom_softc *sc,
    int subunits, void *parent,
    const struct ucom_callback *callback, struct lock *lock)
{
	int subunit;
	int error = 0;

	if ((sc == NULL) ||
	    (subunits <= 0) ||
	    (callback == NULL) ||
	    (lock == NULL)) {
		return (EINVAL);
	}

	/* XXX Do we want our own lock here maybe */
	sc->sc_lock = lock;

	/* allocate a uniq unit number */
	ssc->sc_unit = ucom_unit_alloc();
	if (ssc->sc_unit == -1)
		return (ENOMEM);

	/* generate TTY name string */
	ksnprintf(ssc->sc_ttyname, sizeof(ssc->sc_ttyname),
	    UCOM_TTY_PREFIX "%d", ssc->sc_unit);

	/* create USB request handling process */
	error = usb_proc_create(&ssc->sc_tq, lock, "ucom", USB_PRI_MED);
	if (error) {
		ucom_unit_free(ssc->sc_unit);
		return (error);
	}
	ssc->sc_subunits = subunits;
	ssc->sc_flag = UCOM_FLAG_ATTACHED |
	    UCOM_FLAG_FREE_UNIT;

	if (callback->ucom_free == NULL)
		ssc->sc_flag |= UCOM_FLAG_WAIT_REFS;

	/* increment reference count */
	ucom_ref(ssc);

	for (subunit = 0; subunit < ssc->sc_subunits; subunit++) {
		sc[subunit].sc_subunit = subunit;
		sc[subunit].sc_super = ssc;
		sc[subunit].sc_lock = lock;
		sc[subunit].sc_parent = parent;
		sc[subunit].sc_callback = callback;

		error = ucom_attach_tty(ssc, &sc[subunit]);
		if (error) {
			ucom_detach(ssc, &sc[0]);
			return (error);
		}
		/* increment reference count */
		ucom_ref(ssc);

		/* set subunit attached */
		sc[subunit].sc_flag |= UCOM_FLAG_ATTACHED;
	}

	DPRINTF("tp = %p, unit = %d, subunits = %d\n",
		sc->sc_tty, ssc->sc_unit, ssc->sc_subunits);

	return (0);
}

/*
 * The following function will do nothing if the structure pointed to
 * by "ssc" and "sc" is zero or has already been detached.
 */
void
ucom_detach(struct ucom_super_softc *ssc, struct ucom_softc *sc)
{
	int subunit;

	if (!(ssc->sc_flag & UCOM_FLAG_ATTACHED))
		return;		/* not initialized */

	destroy_dev(sc->sc_cdev);
	destroy_dev(sc->sc_cdev_init);
	destroy_dev(sc->sc_cdev_lock);
	destroy_dev(sc->sc_cdev2);
	destroy_dev(sc->sc_cdev2_init);
	destroy_dev(sc->sc_cdev2_lock);

	lwkt_gettoken(&tty_token);

	if (ssc->sc_sysctl_ttyname != NULL) {
		sysctl_remove_oid(ssc->sc_sysctl_ttyname, 1, 0);
		ssc->sc_sysctl_ttyname = NULL;
	}

	if (ssc->sc_sysctl_ttyports != NULL) {
		sysctl_remove_oid(ssc->sc_sysctl_ttyports, 1, 0);
		ssc->sc_sysctl_ttyports = NULL;
	}

	usb_proc_drain(&ssc->sc_tq);

	for (subunit = 0; subunit < ssc->sc_subunits; subunit++) {
		if (sc[subunit].sc_flag & UCOM_FLAG_ATTACHED) {
			ucom_detach_tty(ssc, &sc[subunit]);

			/* avoid duplicate detach */
			sc[subunit].sc_flag &= ~UCOM_FLAG_ATTACHED;
		}
	}
	usb_proc_free(&ssc->sc_tq);

	ucom_unref(ssc);

	if (ssc->sc_flag & UCOM_FLAG_WAIT_REFS)
		ucom_drain(ssc);

	/* make sure we don't detach twice */
	ssc->sc_flag &= ~UCOM_FLAG_ATTACHED;

	lwkt_reltoken(&tty_token);
}

void
ucom_drain(struct ucom_super_softc *ssc)
{
	lockmgr(&ucom_lock, LK_EXCLUSIVE);
	while (ssc->sc_refs > 0) {
		kprintf("ucom: Waiting for a TTY device to close.\n");
		usb_pause_mtx(&ucom_lock, hz);
	}
	lockmgr(&ucom_lock, LK_RELEASE);
}

void
ucom_drain_all(void *arg)
{
	lockmgr(&ucom_lock, LK_EXCLUSIVE);
	while (ucom_close_refs > 0) {
		kprintf("ucom: Waiting for all detached TTY "
		    "devices to have open fds closed.\n");
		usb_pause_mtx(&ucom_lock, hz);
	}
	lockmgr(&ucom_lock, LK_RELEASE);
}

static int
ucom_attach_tty(struct ucom_super_softc *ssc, struct ucom_softc *sc)
{
	struct tty *tp;
	char buf[32];			/* temporary TTY device name buffer */

	lwkt_gettoken(&tty_token);
	
	sc->sc_tty = tp = ttymalloc(sc->sc_tty);

	if (tp == NULL) {
		lwkt_reltoken(&tty_token);
		return (ENOMEM);
	}

	tp->t_sc = (void *)sc;

	tp->t_oproc = ucom_start;
	tp->t_param = ucom_param;
	tp->t_stop = ucom_stop;

	/* Check if the client has a custom TTY name */
	buf[0] = '\0';
	if (sc->sc_callback->ucom_tty_name) {
		sc->sc_callback->ucom_tty_name(sc, buf,
		    sizeof(buf), ssc->sc_unit, sc->sc_subunit);
	}
	if (buf[0] == 0) {
		/* Use default TTY name */
		if (ssc->sc_subunits > 1) {
			/* multiple modems in one */
			ksnprintf(buf, sizeof(buf), UCOM_TTY_PREFIX "%u.%u",
			    ssc->sc_unit, sc->sc_subunit);
		} else {
			/* single modem */
			ksnprintf(buf, sizeof(buf), UCOM_TTY_PREFIX "%u",
			    ssc->sc_unit);
		}
	}

	sc->sc_cdev = make_dev(&ucom_ops, ssc->sc_unit,
			UID_ROOT, GID_WHEEL, 0600, "ttyU%r", ssc->sc_unit);
	sc->sc_cdev_init = make_dev(&ucom_ops, ssc->sc_unit | CONTROL_INIT_STATE,
			UID_ROOT, GID_WHEEL, 0600, "ttyiU%r", ssc->sc_unit);
	sc->sc_cdev_lock = make_dev(&ucom_ops, ssc->sc_unit | CONTROL_LOCK_STATE,
			UID_ROOT, GID_WHEEL, 0600, "ttylU%r", ssc->sc_unit);
	sc->sc_cdev2 = make_dev(&ucom_ops, ssc->sc_unit | CALLOUT_MASK,
			UID_UUCP, GID_DIALER, 0660, "cuaU%r", ssc->sc_unit);
	sc->sc_cdev2_init = make_dev(&ucom_ops, ssc->sc_unit | CALLOUT_MASK | CONTROL_INIT_STATE,
			UID_UUCP, GID_DIALER, 0660, "cuaiU%r", ssc->sc_unit);
	sc->sc_cdev2_lock = make_dev(&ucom_ops, ssc->sc_unit | CALLOUT_MASK | CONTROL_LOCK_STATE,
			UID_UUCP, GID_DIALER, 0660, "cualU%r", ssc->sc_unit);

	sc->sc_cdev->si_tty = tp;
	sc->sc_cdev_init->si_tty = tp;
	sc->sc_cdev_lock->si_tty = tp;

	sc->sc_cdev->si_drv1 = sc;
	sc->sc_cdev_init->si_drv1 = sc;
	sc->sc_cdev_lock->si_drv1 = sc;

	sc->sc_cdev2->si_drv1 = sc;
	sc->sc_cdev2_init->si_drv1 = sc;
	sc->sc_cdev2_lock->si_drv1 = sc;

	sc->sc_tty = tp;
	
	DPRINTF("ttycreate: %s\n", buf);

	/* Check if this device should be a console */
	if ((ucom_cons_softc == NULL) && 
	    (ssc->sc_unit == ucom_cons_unit) &&
	    (sc->sc_subunit == ucom_cons_subunit)) {

		DPRINTF("unit %d subunit %d is console",
		    ssc->sc_unit, sc->sc_subunit);

		ucom_cons_softc = sc;

		/* XXXDF
		tty_init_console(tp, ucom_cons_baud);
		*/
		tp->t_termios.c_ispeed = ucom_cons_baud;
		tp->t_termios.c_ospeed = ucom_cons_baud;

		UCOM_MTX_LOCK(ucom_cons_softc);
		ucom_cons_rx_low = 0;
		ucom_cons_rx_high = 0;
		ucom_cons_tx_low = 0;
		ucom_cons_tx_high = 0;
		sc->sc_flag |= UCOM_FLAG_CONSOLE;
		
		ucom_open(ucom_cons_softc);
		ucom_param(tp, &tp->t_termios);
		UCOM_MTX_UNLOCK(ucom_cons_softc);
	}

	lwkt_reltoken(&tty_token);
	return (0);
}

static void
ucom_detach_tty(struct ucom_super_softc *ssc, struct ucom_softc *sc)
{
	struct tty *tp = sc->sc_tty;

	DPRINTF("sc = %p, tp = %p\n", sc, sc->sc_tty);

	if (sc->sc_flag & UCOM_FLAG_CONSOLE) {
		UCOM_MTX_LOCK(ucom_cons_softc);
		ucom_close(ucom_cons_softc);
		sc->sc_flag &= ~UCOM_FLAG_CONSOLE;
		UCOM_MTX_UNLOCK(ucom_cons_softc);
		ucom_cons_softc = NULL;
	}

	/* the config thread has been stopped when we get here */

	UCOM_MTX_LOCK(sc);
	sc->sc_flag |= UCOM_FLAG_GONE;
	sc->sc_flag &= ~(UCOM_FLAG_HL_READY | UCOM_FLAG_LL_READY);
	UCOM_MTX_UNLOCK(sc);

	lwkt_gettoken(&tty_token);
	if (tp != NULL) {
		ucom_close_refs++;

		UCOM_MTX_LOCK(sc);
		if (tp->t_state & TS_ISOPEN) {
			kprintf("device still open, forcing close\n");
			(*linesw[tp->t_line].l_close)(tp, 0);
			ttyclose(tp);
		}
		ucom_close(sc);	/* close, if any */

		/*
		 * make sure that read and write transfers are stopped
		 */
		if (sc->sc_callback->ucom_stop_read) {
			(sc->sc_callback->ucom_stop_read) (sc);
		}
		if (sc->sc_callback->ucom_stop_write) {
			(sc->sc_callback->ucom_stop_write) (sc);
		}
		UCOM_MTX_UNLOCK(sc);
	} else {
		DPRINTF("no tty\n");
	}

	dev_ops_remove_minor(&ucom_ops,ssc->sc_unit);

	lwkt_reltoken(&tty_token);
	ucom_unref(ssc);
}

void
ucom_set_pnpinfo_usb(struct ucom_super_softc *ssc, device_t dev)
{
	char buf[64];
	uint8_t iface_index;
	struct usb_attach_arg *uaa;

	ksnprintf(buf, sizeof(buf), "ttyname=" UCOM_TTY_PREFIX
	    "%d ttyports=%d", ssc->sc_unit, ssc->sc_subunits);

	/* Store the PNP info in the first interface for the device */
	uaa = device_get_ivars(dev);
	iface_index = uaa->info.bIfaceIndex;
    
	if (usbd_set_pnpinfo(uaa->device, iface_index, buf) != 0)
		device_printf(dev, "Could not set PNP info\n");

	/*
	 * The following information is also replicated in the PNP-info
	 * string which is registered above:
	 */
	if (ssc->sc_sysctl_ttyname == NULL) {
		/*
		ssc->sc_sysctl_ttyname = SYSCTL_ADD_STRING(NULL,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		    OID_AUTO, "ttyname", CTLFLAG_RD, ssc->sc_ttyname, 0,
		    "TTY device basename");
		*/
	}
	if (ssc->sc_sysctl_ttyports == NULL) {
		/*
		ssc->sc_sysctl_ttyports = SYSCTL_ADD_INT(NULL,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		    OID_AUTO, "ttyports", CTLFLAG_RD,
		    NULL, ssc->sc_subunits, "Number of ports");
		*/
	}
}

static void
ucom_queue_command(struct ucom_softc *sc,
    usb_proc_callback_t *fn, struct termios *pt,
    struct usb_proc_msg *t0, struct usb_proc_msg *t1)
{
	struct ucom_super_softc *ssc = sc->sc_super;
	struct ucom_param_task *task;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (usb_proc_is_gone(&ssc->sc_tq)) {
		DPRINTF("proc is gone\n");
		return;         /* nothing to do */
	}
	/* 
	 * NOTE: The task cannot get executed before we drop the
	 * "sc_lock" lock. It is safe to update fields in the message
	 * structure after that the message got queued.
	 */
	task = (struct ucom_param_task *)
	  usb_proc_msignal(&ssc->sc_tq, t0, t1);

	/* Setup callback and softc pointers */
	task->hdr.pm_callback = fn;
	task->sc = sc;

	/* 
	 * Make a copy of the termios. This field is only present if
	 * the "pt" field is not NULL.
	 */
	if (pt != NULL)
		task->termios_copy = *pt;

	/*
	 * Closing the device should be synchronous.
	 */
	if (fn == ucom_cfg_close)
		usb_proc_mwait(&ssc->sc_tq, t0, t1);

	/*
	 * In case of multiple configure requests,
	 * keep track of the last one!
	 */
	if (fn == ucom_cfg_start_transfers)
		sc->sc_last_start_xfer = &task->hdr;
}

static void
ucom_shutdown(struct ucom_softc *sc)
{
	struct tty *tp = sc->sc_tty;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	DPRINTF("\n");

	/*
	 * Hang up if necessary:
	 */
	if (tp->t_termios.c_cflag & HUPCL) {
		ucom_modem(tp, 0, SER_DTR);
	}
}

/*
 * Return values:
 *    0: normal
 * else: taskqueue is draining or gone
 */
uint8_t
ucom_cfg_is_gone(struct ucom_softc *sc)
{
	struct ucom_super_softc *ssc = sc->sc_super;

	return (usb_proc_is_gone(&ssc->sc_tq));
}

static void
ucom_cfg_start_transfers(struct usb_proc_msg *_task)
{
	struct ucom_cfg_task *task = 
	    (struct ucom_cfg_task *)_task;
	struct ucom_softc *sc = task->sc;

	if (!(sc->sc_flag & UCOM_FLAG_LL_READY)) {
		return;
	}
	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		/* TTY device closed */
		return;
	}

	if (_task == sc->sc_last_start_xfer)
		sc->sc_flag |= UCOM_FLAG_GP_DATA;

	if (sc->sc_callback->ucom_start_read) {
		(sc->sc_callback->ucom_start_read) (sc);
	}
	if (sc->sc_callback->ucom_start_write) {
		(sc->sc_callback->ucom_start_write) (sc);
	}
}

static void
ucom_start_transfers(struct ucom_softc *sc)
{
	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		return;
	}
	/*
	 * Make sure that data transfers are started in both
	 * directions:
	 */
	if (sc->sc_callback->ucom_start_read) {
		(sc->sc_callback->ucom_start_read) (sc);
	}
	if (sc->sc_callback->ucom_start_write) {
		(sc->sc_callback->ucom_start_write) (sc);
	}
}

static void
ucom_cfg_open(struct usb_proc_msg *_task)
{
	struct ucom_cfg_task *task = 
	    (struct ucom_cfg_task *)_task;
	struct ucom_softc *sc = task->sc;

	DPRINTF("\n");

	if (sc->sc_flag & UCOM_FLAG_LL_READY) {

		/* already opened */

	} else {

		sc->sc_flag |= UCOM_FLAG_LL_READY;

		if (sc->sc_callback->ucom_cfg_open) {
			(sc->sc_callback->ucom_cfg_open) (sc);

			/* wait a little */
			usb_pause_mtx(sc->sc_lock, hz / 10);
		}
	}
}

static int
ucom_dev_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ucom_softc *sc = (struct ucom_softc *)dev->si_drv1;
	int error;
	int mynor;

	error = 0;
	mynor = minor(dev);

	if (!(mynor & CALLOUT_MASK)) {
		UCOM_MTX_LOCK(sc);
		error = ucom_open(sc);
		UCOM_MTX_UNLOCK(sc);
	}
	return error;	
}

static int
ucom_open(struct ucom_softc *sc)
{
	int error;
	struct tty *tp;

	int mynor;

	mynor = minor(sc->sc_cdev);

	if (sc->sc_flag & UCOM_FLAG_GONE) {
		return (ENXIO);
	}
	if (sc->sc_flag & UCOM_FLAG_HL_READY) {
		/* already opened */
		return (0);
	}
	DPRINTF("tp = %p\n", sc->sc_tty);

	if (sc->sc_callback->ucom_pre_open) {
		/*
		 * give the lower layer a chance to disallow TTY open, for
		 * example if the device is not present:
		 */
		error = (sc->sc_callback->ucom_pre_open) (sc);
		if (error) {
			return (error);
		}
	}
	sc->sc_flag |= UCOM_FLAG_HL_READY;

	lwkt_gettoken(&tty_token);
	tp = sc->sc_tty;

	crit_enter();

	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		struct termios t;

		tp->t_dev = reference_dev(sc->sc_cdev);
	
                t = mynor & CALLOUT_MASK ? sc->sc_it_out : sc->sc_it_in;

		tp->t_ospeed = 0;
		ucom_param(tp, &t);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		ttychars(tp);
		ttsetwater(tp);

		/* Disable transfers */
		sc->sc_flag &= ~UCOM_FLAG_GP_DATA;

		sc->sc_lsr = 0;
		sc->sc_msr = 0;
		sc->sc_mcr = 0;

		/* reset programmed line state */
		sc->sc_pls_curr = 0;
		sc->sc_pls_set = 0;
		sc->sc_pls_clr = 0;

		/* reset jitter buffer */
		sc->sc_jitterbuf_in = 0;
		sc->sc_jitterbuf_out = 0;

		ucom_queue_command(sc, ucom_cfg_open, NULL,
				&sc->sc_open_task[0].hdr,
				&sc->sc_open_task[1].hdr);

		/* Queue transfer enable command last */
		ucom_queue_command(sc, ucom_cfg_start_transfers, NULL,
				&sc->sc_start_task[0].hdr, 
				&sc->sc_start_task[1].hdr);

		ucom_modem(sc->sc_tty, SER_DTR | SER_RTS, 0);

		ucom_ring(sc, 0);

		ucom_break(sc, 0);
		
		ucom_status_change(sc);

		if (ISSET(sc->sc_msr, SER_DCD)) {
			(*linesw[tp->t_line].l_modem)(tp, 1);
		}
	}
	crit_exit();

	error = ttyopen(sc->sc_cdev, tp);
	if (error) {
		lwkt_reltoken(&tty_token);
		return (error);
	}

	error = (*linesw[tp->t_line].l_open)(sc->sc_cdev, tp);
	if (error) {
		lwkt_reltoken(&tty_token);
		return (error);
	}

	disc_optim(tp, &tp->t_termios, sc);

	lwkt_reltoken(&tty_token);
	
	return (0);
}

static void
ucom_cfg_close(struct usb_proc_msg *_task)
{
	struct ucom_cfg_task *task = 
	    (struct ucom_cfg_task *)_task;
	struct ucom_softc *sc = task->sc;

	DPRINTF("\n");

	if (sc->sc_flag & UCOM_FLAG_LL_READY) {
		sc->sc_flag &= ~UCOM_FLAG_LL_READY;
		if (sc->sc_callback->ucom_cfg_close)
			(sc->sc_callback->ucom_cfg_close) (sc);
	} else {
		/* already closed */
	}
}

static int
ucom_dev_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ucom_softc *sc = (struct ucom_softc *)dev->si_drv1;
	int error;

	UCOM_MTX_LOCK(sc);
	error = ucom_close(sc);
	UCOM_MTX_UNLOCK(sc);
	
	return error;
}

static int
ucom_close(struct ucom_softc *sc)
{
	struct tty *tp = sc->sc_tty;
	int error = 0;

	DPRINTF("tp=%p\n", tp);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		DPRINTF("tp=%p already closed\n", tp);
		return (error);
	}
	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		return(error);
	}
	ucom_shutdown(sc);

	ucom_queue_command(sc, ucom_cfg_close, NULL,
	    &sc->sc_close_task[0].hdr,
	    &sc->sc_close_task[1].hdr);

	sc->sc_flag &= ~(UCOM_FLAG_HL_READY | UCOM_FLAG_RTS_IFLOW);

	if (sc->sc_callback->ucom_stop_read) {
		(sc->sc_callback->ucom_stop_read) (sc);
	}

	lwkt_gettoken(&tty_token);
	crit_enter();
	(*linesw[tp->t_line].l_close)(tp, 0); /* XXX: flags */
	disc_optim(tp, &tp->t_termios, sc);
	ttyclose(tp);
	crit_exit();

	if (tp->t_dev) {
		release_dev(tp->t_dev);
		tp->t_dev = NULL;
	}
	/* XXX: Detach wakeup */
	lwkt_reltoken(&tty_token);

	return (error);
}

#if 0 /* XXX */
static void
ucom_inwakeup(struct tty *tp)
{
	struct ucom_softc *sc = tty_softc(tp);
	uint16_t pos;

	if (sc == NULL)
		return;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	DPRINTF("tp=%p\n", tp);

	if (ttydisc_can_bypass(tp) != 0 || 
	    (sc->sc_flag & UCOM_FLAG_HL_READY) == 0 ||
	    (sc->sc_flag & UCOM_FLAG_INWAKEUP) != 0) {
		return;
	}

	/* prevent recursion */
	sc->sc_flag |= UCOM_FLAG_INWAKEUP;

	pos = sc->sc_jitterbuf_out;

	while (sc->sc_jitterbuf_in != pos) {
		int c;

		c = (char)sc->sc_jitterbuf[pos];

		if (ttydisc_rint(tp, c, 0) == -1)
			break;
		pos++;
		if (pos >= UCOM_JITTERBUF_SIZE)
			pos -= UCOM_JITTERBUF_SIZE;
	}

	sc->sc_jitterbuf_out = pos;

	/* clear RTS in async fashion */
	if ((sc->sc_jitterbuf_in == pos) && 
	    (sc->sc_flag & UCOM_FLAG_RTS_IFLOW))
		ucom_rts(sc, 0);

	sc->sc_flag &= ~UCOM_FLAG_INWAKEUP;
}
#endif

static int
ucom_dev_read(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
        struct ucom_softc *sc;
        struct tty *tp;
        int error;

	sc = 0;

        lwkt_gettoken(&tty_token);

        tp = dev->si_tty;
	KKASSERT(tp!=NULL);
	sc = tp->t_sc;
	KKASSERT(sc!=NULL);

        DPRINTF("tp = %p, flag = 0x%x\n", tp, ap->a_ioflag);

	UCOM_MTX_LOCK(sc);
        error = (*linesw[tp->t_line].l_read)(tp, ap->a_uio, ap->a_ioflag);
	UCOM_MTX_UNLOCK(sc);

        DPRINTF("error = %d\n", error);

        lwkt_reltoken(&tty_token);
        return (error);
}

static int
ucom_dev_write(struct dev_write_args *ap)
{
        cdev_t dev = ap->a_head.a_dev;
        struct ucom_softc *sc;
        struct tty *tp;
        int error;

        lwkt_gettoken(&tty_token);
        tp = dev->si_tty;
	KKASSERT(tp!=NULL);
	sc = tp->t_sc;
	KKASSERT(sc!=NULL);

        DPRINTF("tp = %p, flag = 0x%x\n", tp, ap->a_ioflag);

	UCOM_MTX_LOCK(sc);
        error = (*linesw[tp->t_line].l_write)(tp, ap->a_uio, ap->a_ioflag);
	UCOM_MTX_UNLOCK(sc);

        DPRINTF("ucomwrite: error = %d\n", error);

        lwkt_reltoken(&tty_token);
        return (error);
}

static int
ucom_dev_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ucom_softc *sc = (struct ucom_softc *)dev->si_drv1;
	u_long cmd = ap->a_cmd;
	caddr_t data = ap->a_data;
	struct tty *tp = sc->sc_tty;
	int d;
	int error;
	int mynor;

	UCOM_MTX_LOCK(sc);
	lwkt_gettoken(&tty_token);

	mynor = minor(dev);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		lwkt_reltoken(&tty_token);
		return (EIO);
	}
	DPRINTF("cmd = 0x%08lx\n", cmd);
	if (mynor & CONTROL_MASK) {
		struct termios *ct;

                switch (mynor & CONTROL_MASK) {
                case CONTROL_INIT_STATE:
                        ct = mynor & CALLOUT_MASK ? &sc->sc_it_out : &sc->sc_it_in;
                        break;
                case CONTROL_LOCK_STATE:
                        ct = mynor & CALLOUT_MASK ? &sc->sc_lt_out : &sc->sc_lt_in;
                        break;
                default:
                        lwkt_reltoken(&tty_token);
                        return (ENODEV);        /* /dev/nodev */
                }
                switch (ap->a_cmd) {
                case TIOCSETA:
                        error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
                        if (error != 0) {
                                lwkt_reltoken(&tty_token);
                                return (error);
                        }
                        *ct = *(struct termios *)data;
                        lwkt_reltoken(&tty_token);
                        return (0);
                case TIOCGETA:
                        *(struct termios *)data = *ct;
                        lwkt_reltoken(&tty_token);
                        return (0);
                case TIOCGETD:
                        *(int *)data = TTYDISC;
                        lwkt_reltoken(&tty_token);
                        return (0);
                case TIOCGWINSZ:
                        bzero(data, sizeof(struct winsize));
                        lwkt_reltoken(&tty_token);
                        return (0);
                default:
                        lwkt_reltoken(&tty_token);
                        return (ENOTTY);
                }
	}

	error = (*linesw[tp->t_line].l_ioctl)(tp, ap->a_cmd, ap->a_data,
                                              ap->a_fflag, ap->a_cred);

	if (error != ENOIOCTL) {
                DPRINTF("ucomioctl: l_ioctl: error = %d\n", error);
                lwkt_reltoken(&tty_token);
		UCOM_MTX_UNLOCK(sc);
                return (error);
        }

        crit_enter();

	error = ttioctl(tp, ap->a_cmd, ap->a_data, ap->a_fflag);
        disc_optim(tp, &tp->t_termios, sc);
        if (error != ENOIOCTL) {
                crit_exit();
                DPRINTF("ucomioctl: ttioctl: error = %d\n", error);
                lwkt_reltoken(&tty_token);
		UCOM_MTX_UNLOCK(sc);

                return (error);
        }

	error = 0;

	switch (cmd) {
#if 0 /* XXXDF */
	case TIOCSRING:
		ucom_ring(sc, 1);
		error = 0;
		break;
	case TIOCCRING:
		ucom_ring(sc, 0);
		error = 0;
		break;
#endif
	case TIOCSBRK:
		ucom_break(sc, 1);
		error = 0;
		break;
	case TIOCCBRK:
		ucom_break(sc, 0);
		error = 0;
		break;
	case TIOCSDTR:
		ucom_dtr(sc, 1);
		break;
	case TIOCCDTR:
		ucom_dtr(sc, 0);
		break;
	case TIOCMSET:
		d = *(int *)ap->a_data;
		DPRINTF("ucomioctl: TIOCMSET, 0x%x\n", d);
		ucom_modem(tp, ucom_fromtio(d), 0);
		break;
	case TIOCMGET:
		d = ucom_modem(tp, 0, 0);
		DPRINTF("ucomioctl: TIOCMGET, 0x%x\n", d);
		*(int *)ap->a_data = ucom_totio(d);
		break;
	case TIOCMBIS:
		d = *(int *)ap->a_data;
		ucom_modem(tp, ucom_fromtio(d), 0);
		break;
	case TIOCMBIC:
		d = *(int *)ap->a_data;
		ucom_modem(tp, 0, ucom_fromtio(d));
		break;
	default:
		if (sc->sc_callback->ucom_ioctl) {
			error = (sc->sc_callback->ucom_ioctl)
			    (sc, cmd, data, 0, curthread);
			if (error>=0) {
				crit_exit();

				lwkt_reltoken(&tty_token);
				UCOM_MTX_UNLOCK(sc);

				return(error);	
			}
		} else {
			error = ENOIOCTL;
		}
		if (error == ENOIOCTL)
			error = pps_ioctl(cmd, data, &sc->sc_pps);
		break;
	}
	crit_exit();

	lwkt_reltoken(&tty_token);
	UCOM_MTX_UNLOCK(sc);

	return (error);
}

static int
ucom_totio(int bits)
{
	int rbits = 0;

	SET(bits, TIOCM_LE);

	if (ISSET(bits, SER_DTR)) {
		SET(rbits, TIOCM_DTR);
	}
	if (ISSET(bits, SER_RTS)) {
		SET(rbits, TIOCM_RTS);
	}
	if (ISSET(bits, SER_CTS)) {
		SET(rbits, TIOCM_CTS);
	}
	if (ISSET(bits, SER_DCD)) {
		SET(rbits, TIOCM_CD);
	}
	if (ISSET(bits, SER_DSR)) {
		SET(rbits, TIOCM_DSR);
	}
	if (ISSET(bits, SER_RI)) {
		SET(rbits, TIOCM_RI);
	}

	return (rbits);
}

static int
ucom_fromtio(int bits)
{
	int rbits = 0;

	if (ISSET(bits, TIOCM_DTR)) {
		SET(rbits, SER_DTR);
	}
	if (ISSET(bits, TIOCM_RTS)) {
		SET(rbits, SER_RTS);
	}
	if (ISSET(bits, TIOCM_CTS)) {
		SET(rbits, SER_CTS);
	}
	if (ISSET(bits, TIOCM_CD)) {
		SET(rbits, SER_DCD);
	}
	if (ISSET(bits, TIOCM_DSR)) {
		SET(rbits, SER_DSR);
	}
	if (ISSET(bits, TIOCM_RI)) {
		SET(rbits, SER_RI);
	}

	return (rbits);
}

static int
ucom_modem(struct tty *tp, int sigon, int sigoff)
{
	struct ucom_softc *sc = (struct ucom_softc *)tp->t_sc;
	uint8_t onoff;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		return (0);
	}
	if ((sigon == 0) && (sigoff == 0)) {

		if (sc->sc_mcr & SER_DTR) {
			sigon |= SER_DTR;
		}
		if (sc->sc_mcr & SER_RTS) {
			sigon |= SER_RTS;
		}
		if (sc->sc_msr & SER_CTS) {
			sigon |= SER_CTS;
		}
		if (sc->sc_msr & SER_DCD) {
			sigon |= SER_DCD;
		}
		if (sc->sc_msr & SER_DSR) {
			sigon |= SER_DSR;
		}
		if (sc->sc_msr & SER_RI) {
			sigon |= SER_RI;
		}
		return (sigon);
	}
	if (sigon & SER_DTR) {
		sc->sc_mcr |= SER_DTR;
	}
	if (sigoff & SER_DTR) {
		sc->sc_mcr &= ~SER_DTR;
	}
	if (sigon & SER_RTS) {
		sc->sc_mcr |= SER_RTS;
	}
	if (sigoff & SER_RTS) {
		sc->sc_mcr &= ~SER_RTS;
	}
	onoff = (sc->sc_mcr & SER_DTR) ? 1 : 0;
	ucom_dtr(sc, onoff);

	onoff = (sc->sc_mcr & SER_RTS) ? 1 : 0;
	ucom_rts(sc, onoff);

	return (0);
}

static void
ucom_cfg_line_state(struct usb_proc_msg *_task)
{
	struct ucom_cfg_task *task = 
	    (struct ucom_cfg_task *)_task;
	struct ucom_softc *sc = task->sc;
	uint8_t notch_bits;
	uint8_t any_bits;
	uint8_t prev_value;
	uint8_t last_value;
	uint8_t mask;

	if (!(sc->sc_flag & UCOM_FLAG_LL_READY)) {
		return;
	}

	mask = 0;
	/* compute callback mask */
	if (sc->sc_callback->ucom_cfg_set_dtr)
		mask |= UCOM_LS_DTR;
	if (sc->sc_callback->ucom_cfg_set_rts)
		mask |= UCOM_LS_RTS;
	if (sc->sc_callback->ucom_cfg_set_break)
		mask |= UCOM_LS_BREAK;
	if (sc->sc_callback->ucom_cfg_set_ring)
		mask |= UCOM_LS_RING;

	/* compute the bits we are to program */
	notch_bits = (sc->sc_pls_set & sc->sc_pls_clr) & mask;
	any_bits = (sc->sc_pls_set | sc->sc_pls_clr) & mask;
	prev_value = sc->sc_pls_curr ^ notch_bits;
	last_value = sc->sc_pls_curr;

	/* reset programmed line state */
	sc->sc_pls_curr = 0;
	sc->sc_pls_set = 0;
	sc->sc_pls_clr = 0;

	/* ensure that we don't lose any levels */
	if (notch_bits & UCOM_LS_DTR)
		sc->sc_callback->ucom_cfg_set_dtr(sc,
		    (prev_value & UCOM_LS_DTR) ? 1 : 0);
	if (notch_bits & UCOM_LS_RTS)
		sc->sc_callback->ucom_cfg_set_rts(sc,
		    (prev_value & UCOM_LS_RTS) ? 1 : 0);
	if (notch_bits & UCOM_LS_BREAK)
		sc->sc_callback->ucom_cfg_set_break(sc,
		    (prev_value & UCOM_LS_BREAK) ? 1 : 0);
	if (notch_bits & UCOM_LS_RING)
		sc->sc_callback->ucom_cfg_set_ring(sc,
		    (prev_value & UCOM_LS_RING) ? 1 : 0);

	/* set last value */
	if (any_bits & UCOM_LS_DTR)
		sc->sc_callback->ucom_cfg_set_dtr(sc,
		    (last_value & UCOM_LS_DTR) ? 1 : 0);
	if (any_bits & UCOM_LS_RTS)
		sc->sc_callback->ucom_cfg_set_rts(sc,
		    (last_value & UCOM_LS_RTS) ? 1 : 0);
	if (any_bits & UCOM_LS_BREAK)
		sc->sc_callback->ucom_cfg_set_break(sc,
		    (last_value & UCOM_LS_BREAK) ? 1 : 0);
	if (any_bits & UCOM_LS_RING)
		sc->sc_callback->ucom_cfg_set_ring(sc,
		    (last_value & UCOM_LS_RING) ? 1 : 0);
}

static void
ucom_line_state(struct ucom_softc *sc,
    uint8_t set_bits, uint8_t clear_bits)
{
	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		return;
	}

	DPRINTF("on=0x%02x, off=0x%02x\n", set_bits, clear_bits);

	/* update current programmed line state */
	sc->sc_pls_curr |= set_bits;
	sc->sc_pls_curr &= ~clear_bits;
	sc->sc_pls_set |= set_bits;
	sc->sc_pls_clr |= clear_bits;

	/* defer driver programming */
	ucom_queue_command(sc, ucom_cfg_line_state, NULL,
	    &sc->sc_line_state_task[0].hdr, 
	    &sc->sc_line_state_task[1].hdr);
}

static void
ucom_ring(struct ucom_softc *sc, uint8_t onoff)
{
	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		ucom_line_state(sc, UCOM_LS_RING, 0);
	else
		ucom_line_state(sc, 0, UCOM_LS_RING);
}

static void
ucom_break(struct ucom_softc *sc, uint8_t onoff)
{
	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		ucom_line_state(sc, UCOM_LS_BREAK, 0);
	else
		ucom_line_state(sc, 0, UCOM_LS_BREAK);
}

static void
ucom_dtr(struct ucom_softc *sc, uint8_t onoff)
{
	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		ucom_line_state(sc, UCOM_LS_DTR, 0);
	else
		ucom_line_state(sc, 0, UCOM_LS_DTR);
}

static void
ucom_rts(struct ucom_softc *sc, uint8_t onoff)
{
	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		ucom_line_state(sc, UCOM_LS_RTS, 0);
	else
		ucom_line_state(sc, 0, UCOM_LS_RTS);
}

static void
ucom_cfg_status_change(struct usb_proc_msg *_task)
{
	struct ucom_cfg_task *task = 
	    (struct ucom_cfg_task *)_task;
	struct ucom_softc *sc = task->sc;
	struct tty *tp;
	uint8_t new_msr;
	uint8_t new_lsr;
	uint8_t msr_delta;
	uint8_t lsr_delta;

	tp = sc->sc_tty;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (!(sc->sc_flag & UCOM_FLAG_LL_READY)) {
		return;
	}
	if (sc->sc_callback->ucom_cfg_get_status == NULL) {
		return;
	}
	/* get status */

	new_msr = 0;
	new_lsr = 0;

	(sc->sc_callback->ucom_cfg_get_status) (sc, &new_lsr, &new_msr);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		/* TTY device closed */
		return;
	}
	msr_delta = (sc->sc_msr ^ new_msr);
	lsr_delta = (sc->sc_lsr ^ new_lsr);

	sc->sc_msr = new_msr;
	sc->sc_lsr = new_lsr;

#if 0	/* missing pps_capture */
	/*
	 * Time pulse counting support. Note that both CTS and DCD are
	 * active-low signals. The status bit is high to indicate that
	 * the signal on the line is low, which corresponds to a PPS
	 * clear event.
	 */
	switch(ucom_pps_mode) {
	case 1:
		if ((sc->sc_pps.ppsparam.mode & PPS_CAPTUREBOTH) &&
		    (msr_delta & SER_CTS)) {
			pps_capture(&sc->sc_pps);
			pps_event(&sc->sc_pps, (sc->sc_msr & SER_CTS) ?
			    PPS_CAPTURECLEAR : PPS_CAPTUREASSERT);
		}
		break;
	case 2:
		if ((sc->sc_pps.ppsparam.mode & PPS_CAPTUREBOTH) &&
		    (msr_delta & SER_DCD)) {
			pps_capture(&sc->sc_pps);
			pps_event(&sc->sc_pps, (sc->sc_msr & SER_DCD) ?
			    PPS_CAPTURECLEAR : PPS_CAPTUREASSERT);
		}
		break;
	default:
		break;
	}
#endif

	if (msr_delta & SER_DCD) {

		int onoff = (sc->sc_msr & SER_DCD) ? 1 : 0;

		DPRINTF("DCD changed to %d\n", onoff);

		(*linesw[tp->t_line].l_modem)(tp, onoff);
	}

	if ((lsr_delta & ULSR_BI) && (sc->sc_lsr & ULSR_BI)) {

		DPRINTF("BREAK detected\n");
        	(*linesw[tp->t_line].l_rint)(0, tp);

		/*
		ttydisc_rint(tp, 0, TRE_BREAK);
		ttydisc_rint_done(tp);
		*/
	}

	if ((lsr_delta & ULSR_FE) && (sc->sc_lsr & ULSR_FE)) {

		DPRINTF("Frame error detected\n");
        	(*linesw[tp->t_line].l_rint)(0, tp);

		/*
		ttydisc_rint(tp, 0, TRE_FRAMING);
		ttydisc_rint_done(tp);
		*/
	}

	if ((lsr_delta & ULSR_PE) && (sc->sc_lsr & ULSR_PE)) {

		DPRINTF("Parity error detected\n");
        	(*linesw[tp->t_line].l_rint)(0, tp);
		/*
		ttydisc_rint(tp, 0, TRE_PARITY);
		ttydisc_rint_done(tp);
		*/
	}
}

void
ucom_status_change(struct ucom_softc *sc)
{
	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (sc->sc_flag & UCOM_FLAG_CONSOLE)
		return;		/* not supported */

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		return;
	}
	DPRINTF("\n");

	ucom_queue_command(sc, ucom_cfg_status_change, NULL,
	    &sc->sc_status_task[0].hdr,
	    &sc->sc_status_task[1].hdr);
}

static void
ucom_cfg_param(struct usb_proc_msg *_task)
{
	struct ucom_param_task *task = 
	    (struct ucom_param_task *)_task;
	struct ucom_softc *sc = task->sc;

	if (!(sc->sc_flag & UCOM_FLAG_LL_READY)) {
		return;
	}
	if (sc->sc_callback->ucom_cfg_param == NULL) {
		return;
	}

	(sc->sc_callback->ucom_cfg_param) (sc, &task->termios_copy);

	/* wait a little */
	usb_pause_mtx(sc->sc_lock, hz / 10);
}

static int
ucom_param(struct tty *tp, struct termios *t)
{
	struct ucom_softc *sc = (struct ucom_softc *)tp->t_sc;
	uint8_t opened;
	int error;

	lwkt_gettoken(&tty_token);
	UCOM_MTX_ASSERT(sc, MA_OWNED);

	opened = 0;
	error = 0;

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {

		/* XXX the TTY layer should call "open()" first! */
		/*
		 * Not quite: Its ordering is partly backwards, but
		 * some parameters must be set early in ttydev_open(),
		 * possibly before calling ttydevsw_open().
		 */
		error = ucom_open(sc);

		if (error) {
			goto done;
		}
		opened = 1;
	}
	DPRINTF("sc = %p\n", sc);

	/* Check requested parameters. */
	if (t->c_ispeed && (t->c_ispeed != t->c_ospeed)) {
		/* XXX c_ospeed == 0 is perfectly valid. */
		DPRINTF("mismatch ispeed and ospeed\n");
		error = EINVAL;
		goto done;
	}
	t->c_ispeed = t->c_ospeed;

	if (sc->sc_callback->ucom_pre_param) {
		/* Let the lower layer verify the parameters */
		error = (sc->sc_callback->ucom_pre_param) (sc, t);
		if (error) {
			DPRINTF("callback error = %d\n", error);
			goto done;
		}
	}

	/* Disable transfers */
	sc->sc_flag &= ~UCOM_FLAG_GP_DATA;

	/* Queue baud rate programming command first */
	ucom_queue_command(sc, ucom_cfg_param, t,
	    &sc->sc_param_task[0].hdr,
	    &sc->sc_param_task[1].hdr);

	/* Queue transfer enable command last */
	ucom_queue_command(sc, ucom_cfg_start_transfers, NULL,
	    &sc->sc_start_task[0].hdr, 
	    &sc->sc_start_task[1].hdr);

	if (t->c_cflag & CRTS_IFLOW) {
		sc->sc_flag |= UCOM_FLAG_RTS_IFLOW;
	} else if (sc->sc_flag & UCOM_FLAG_RTS_IFLOW) {
		sc->sc_flag &= ~UCOM_FLAG_RTS_IFLOW;
		ucom_modem(tp, SER_RTS, 0);
	}
done:
	if (error) {
		if (opened) {
			ucom_close(sc);
		}
	}

	lwkt_reltoken(&tty_token);	
	return (error);
}

static void
ucom_start(struct tty *tp)
{
	struct ucom_softc *sc = (struct ucom_softc *)tp->t_sc;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	DPRINTF("sc = %p\n", sc);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		/* The higher layer is not ready */
		return;
	}

	lwkt_gettoken(&tty_token);
	crit_enter();

	if (tp->t_state & TS_TBLOCK) {
		if (ISSET(sc->sc_mcr, SER_RTS) &&
		    ISSET(sc->sc_flag, UCOM_FLAG_RTS_IFLOW)) {
			DPRINTF("ucom_start: clear RTS\n");
			(void)ucom_modem(tp, 0, SER_RTS);
		}
	} else {
		if (!ISSET(sc->sc_mcr, SER_RTS) &&
		    tp->t_rawq.c_cc <= tp->t_ilowat &&
		    ISSET(sc->sc_flag, UCOM_FLAG_RTS_IFLOW)) {
			DPRINTF("ucom_start: set RTS\n");
			(void)ucom_modem(tp, SER_RTS, 0);
		}
	}

	if (ISSET(tp->t_state, TS_BUSY | TS_TIMEOUT | TS_TTSTOP)) {
		ttwwakeup(tp);
		DPRINTF("ucom_start: stopped\n");
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

	DPRINTF("about to start write?\n");
	ucom_start_transfers(sc);

	ttwwakeup(tp);

out:
	crit_exit();
	lwkt_reltoken(&tty_token);
}

static void
ucom_stop(struct tty *tp, int flag)
{
	struct ucom_softc *sc = (struct ucom_softc *)tp->t_sc;

	DPRINTF("sc = %p, x = 0x%x\n", sc, flag);

	lwkt_gettoken(&tty_token);
	if (flag & FREAD) {
		/*   
		 * This is just supposed to flush pending receive data,
		 * not stop the reception of data entirely!
		 */
		DPRINTF("read\n");
		if (sc->sc_callback->ucom_stop_read) {
			(sc->sc_callback->ucom_stop_read) (sc);
		}
		if (sc->sc_callback->ucom_start_read) {
			(sc->sc_callback->ucom_start_read) (sc);
		}
/*		ucomstopread(sc);
		ucomstartread(sc);
*/	}    

	if (flag & FWRITE) {
		DPRINTF("write\n");
		crit_enter();
		if (ISSET(tp->t_state, TS_BUSY)) {
			/* XXX do what? */
			if (!ISSET(tp->t_state, TS_TTSTOP))
				SET(tp->t_state, TS_FLUSH);
		}    
		crit_exit();
	}    

	DPRINTF("done\n");
	lwkt_reltoken(&tty_token);
}

/*------------------------------------------------------------------------*
 *	ucom_get_data
 * Input values:
 * len: maximum length of data to get
 * 
 * Get data from the TTY layer
 *
 * Return values:
 * 0: No data is available.
 * Else: Data is available.
 *------------------------------------------------------------------------*/

/* Copy data from the tty layer to usb */
uint8_t
ucom_get_data(struct ucom_softc *sc, struct usb_page_cache *pc,
    uint32_t offset, uint32_t len, uint32_t *actlen)
{
	struct usb_page_search res;
	struct tty *tp = sc->sc_tty;
	uint32_t cnt;
	uint32_t offset_orig;
	
	DPRINTF("\n");

	UCOM_MTX_ASSERT(sc, MA_OWNED);
	if (sc->sc_flag & UCOM_FLAG_CONSOLE) {
		unsigned int temp;

		/* get total TX length */

		temp = ucom_cons_tx_high - ucom_cons_tx_low;
		temp %= UCOM_CONS_BUFSIZE;

		/* limit TX length */

		if (temp > (UCOM_CONS_BUFSIZE - ucom_cons_tx_low))
			temp = (UCOM_CONS_BUFSIZE - ucom_cons_tx_low);

		if (temp > len)
			temp = len;

		/* copy in data */

		usbd_copy_in(pc, offset, ucom_cons_tx_buf + ucom_cons_tx_low, temp);

		/* update counters */

		ucom_cons_tx_low += temp;
		ucom_cons_tx_low %= UCOM_CONS_BUFSIZE;

		/* store actual length */

		*actlen = temp;

		return (temp ? 1 : 0);
	}

	if (tty_gone(tp) ||
	    !(sc->sc_flag & UCOM_FLAG_GP_DATA)) {
		actlen[0] = 0;
		return (0);		/* multiport device polling */
	}

	offset_orig = offset;

	lwkt_gettoken(&tty_token);
	crit_enter();
	while (len != 0) {
		usbd_get_page(pc, offset, &res);

		/* Buffer bigger than max requested data */
		if (res.length > len) {
			res.length = len;
		}
		/* copy data directly into USB buffer */
		SET(tp->t_state, TS_BUSY);
		cnt = q_to_b(&tp->t_outq, res.buffer, len); 
		if (cnt == 0) {
			DPRINTF("ucom_get_data: cnt == 0\n");
			CLR(tp->t_state, TS_BUSY);
			break;
		}

		CLR(tp->t_state, TS_BUSY);

		/* XXX mp: This breaks avrdude,
                           does the flush need to happen
                           elsewhere?
		if (ISSET(tp->t_state, TS_FLUSH))
			CLR(tp->t_state, TS_FLUSH);
		else
			ndflush(&tp->t_outq,cnt);
		*/
	       
		offset += cnt;
		len -= cnt;

		if (cnt < res.length) {
			/* end of buffer */
			break;
		}
	}
	crit_exit();
	lwkt_reltoken(&tty_token);

	actlen[0] = offset - offset_orig;

	DPRINTF("cnt=%d\n", actlen[0]);

	if (actlen[0] == 0) {
		return (0);
	}
	return (1);
}

/*
 * Write data to the tty layer
 */

void
ucom_put_data(struct ucom_softc *sc, struct usb_page_cache *pc,
    uint32_t offset, uint32_t len)
{
	struct usb_page_search res;
	struct tty *tp = sc->sc_tty;
	char *buf;
	uint32_t cnt;
	int lostcc;

	DPRINTF("\n");

	UCOM_MTX_ASSERT(sc, MA_OWNED);
	lwkt_gettoken(&tty_token);

	if (sc->sc_flag & UCOM_FLAG_CONSOLE) {
		unsigned int temp;

		/* get maximum RX length */

		temp = (UCOM_CONS_BUFSIZE - 1) - ucom_cons_rx_high + ucom_cons_rx_low;
		temp %= UCOM_CONS_BUFSIZE;

		/* limit RX length */

		if (temp > (UCOM_CONS_BUFSIZE - ucom_cons_rx_high))
			temp = (UCOM_CONS_BUFSIZE - ucom_cons_rx_high);

		if (temp > len)
			temp = len;

		/* copy out data */

		usbd_copy_out(pc, offset, ucom_cons_rx_buf + ucom_cons_rx_high, temp);

		/* update counters */

		ucom_cons_rx_high += temp;
		ucom_cons_rx_high %= UCOM_CONS_BUFSIZE;

		lwkt_reltoken(&tty_token);
		return;
	}

	if (tty_gone(tp)) {
		lwkt_reltoken(&tty_token);
		return;			/* multiport device polling */
	}
	if (len == 0) {
		lwkt_reltoken(&tty_token);
		return;			/* no data */
	}

	/* set a flag to prevent recursation ? */

	crit_enter();
	while (len > 0) {
		usbd_get_page(pc, offset, &res);

		if (res.length > len) {
			res.length = len;
		}
		len -= res.length;
		offset += res.length;

		/* pass characters to tty layer */

		buf = res.buffer;
		cnt = res.length;

		/* first check if we can pass the buffer directly */

		if (tp->t_state & TS_CAN_BYPASS_L_RINT) {
			/* clear any jitter buffer */
			sc->sc_jitterbuf_in = 0;
			sc->sc_jitterbuf_out = 0;

			if (tp->t_rawq.c_cc + cnt > tp->t_ihiwat
			    && (sc->sc_flag & UCOM_FLAG_RTS_IFLOW
				|| tp->t_iflag & IXOFF)
			    && !(tp->t_state & TS_TBLOCK))
			       ttyblock(tp);
			lostcc = b_to_q((char *)buf, cnt, &tp->t_rawq);
			tp->t_rawcc += cnt;
			if (sc->hotchar) {
				while (cnt) {
					if (*buf == sc->hotchar)
						break;
					--cnt;
					++buf;
				}
				if (cnt) 
					setsofttty();
			}
			ttwakeup(tp);
			if (tp->t_state & TS_TTSTOP
			    && (tp->t_iflag & IXANY
				|| tp->t_cc[VSTART] == tp->t_cc[VSTOP])) {
				tp->t_state &= ~TS_TTSTOP;
				tp->t_lflag &= ~FLUSHO;
				ucom_start(tp);
			}	
			if (lostcc > 0)
				kprintf("lost %d chars\n", lostcc);

			/*
			if (ttydisc_rint_bypass(tp, buf, cnt) != cnt) {
				DPRINTF("tp=%p, data lost\n", tp);
			}
			*/
			continue;
		} else {
		/* need to loop */
			for (cnt = 0; cnt != res.length; cnt++) {
				if (sc->sc_jitterbuf_in != sc->sc_jitterbuf_out ||
				    (*linesw[tp->t_line].l_rint)(buf[cnt], tp) == -1) {
					uint16_t end;
					uint16_t pos;

					pos = sc->sc_jitterbuf_in;
					end = sc->sc_jitterbuf_out +
					    UCOM_JITTERBUF_SIZE - 1;
					
					if (end >= UCOM_JITTERBUF_SIZE)
						end -= UCOM_JITTERBUF_SIZE;

					for (; cnt != res.length; cnt++) {
						if (pos == end)
							break;
						sc->sc_jitterbuf[pos] = buf[cnt];
						pos++;
						if (pos >= UCOM_JITTERBUF_SIZE)
							pos -= UCOM_JITTERBUF_SIZE;
					}

					sc->sc_jitterbuf_in = pos;

					/* set RTS in async fashion */
					if (sc->sc_flag & UCOM_FLAG_RTS_IFLOW)
						ucom_rts(sc, 1);

					DPRINTF("tp=%p, lost %d "
					    "chars\n", tp, res.length - cnt);
					break;
				}
			}
		}
	}
	crit_exit();
	lwkt_reltoken(&tty_token);
	/*
	ttydisc_rint_done(tp);
	*/
}

#if 0 /* XXX */
static void
ucom_free(void *xsc)
{
	struct ucom_softc *sc = xsc;

	if (sc->sc_callback->ucom_free != NULL)
		sc->sc_callback->ucom_free(sc);
	else
		/*ucom_unref(sc->sc_super) XXX hack, see end of ucom_detach_tty() */;

	lockmgr(&ucom_lock, LK_EXCLUSIVE);
	ucom_close_refs--;
	lockmgr(&ucom_lock, LK_RELEASE);
}

static cn_probe_t ucom_cnprobe;
static cn_init_t ucom_cninit;
static cn_term_t ucom_cnterm;
static cn_getc_t ucom_cngetc;
static cn_putc_t ucom_cnputc;

/*
static cn_grab_t ucom_cngrab;
static cn_ungrab_t ucom_cnungrab;
CONSOLE_DRIVER(ucom);
*/

static void
ucom_cnprobe(struct consdev  *cp)
{
	if (ucom_cons_unit != -1)
		cp->cn_pri = CN_NORMAL;
	else
		cp->cn_pri = CN_DEAD;

	/*
	strlcpy(cp->cn_name, "ucom", sizeof(cp->cn_name));
	*/
}

static void
ucom_cninit(struct consdev  *cp)
{
}

static void
ucom_cnterm(struct consdev  *cp)
{
}

static void
ucom_cngrab(struct consdev *cp)
{
}

static void
ucom_cnungrab(struct consdev *cp)
{
}

static int
ucom_cngetc(struct consdev *cd)
{
	struct ucom_softc *sc = ucom_cons_softc;
	int c;

	if (sc == NULL)
		return (-1);

	UCOM_MTX_LOCK(sc);

	if (ucom_cons_rx_low != ucom_cons_rx_high) {
		c = ucom_cons_rx_buf[ucom_cons_rx_low];
		ucom_cons_rx_low ++;
		ucom_cons_rx_low %= UCOM_CONS_BUFSIZE;
	} else {
		c = -1;
	}

	/* start USB transfers */
	ucom_outwakeup(sc->sc_tty);

	UCOM_MTX_UNLOCK(sc);

	/* poll if necessary */
	/*
	if (kdb_active && sc->sc_callback->ucom_poll)
		(sc->sc_callback->ucom_poll) (sc);
	*/
	return (c);
}

static void
ucom_cnputc(void *cd, int c)
	/*
ucom_cnputc(struct consdev *cd, int c)
	*/

{
	struct ucom_softc *sc = ucom_cons_softc;
	unsigned int temp;

	if (sc == NULL)
		return;

 repeat:

	UCOM_MTX_LOCK(sc);

	/* compute maximum TX length */

	temp = (UCOM_CONS_BUFSIZE - 1) - ucom_cons_tx_high + ucom_cons_tx_low;
	temp %= UCOM_CONS_BUFSIZE;

	if (temp) {
		ucom_cons_tx_buf[ucom_cons_tx_high] = c;
		ucom_cons_tx_high ++;
		ucom_cons_tx_high %= UCOM_CONS_BUFSIZE;
	}

	/* start USB transfers */
	ucom_outwakeup(sc->sc_tty);

	UCOM_MTX_UNLOCK(sc);

	/* poll if necessary */
#if 0 /* XXX */
	if (kdb_active && sc->sc_callback->ucom_poll) {
		(sc->sc_callback->ucom_poll) (sc);
		/* simple flow control */
		if (temp == 0)
			goto repeat;
	}
#endif
}
#endif
/*------------------------------------------------------------------------*
 *	ucom_ref
 *
 * This function will increment the super UCOM reference count.
 *------------------------------------------------------------------------*/
void
ucom_ref(struct ucom_super_softc *ssc)
{
	lockmgr(&ucom_lock, LK_EXCLUSIVE);
	ssc->sc_refs++;
	lockmgr(&ucom_lock, LK_RELEASE);
}

/*------------------------------------------------------------------------*
 *	ucom_free_unit
 *
 * This function will free the super UCOM's allocated unit
 * number. This function can be called on a zero-initialized
 * structure. This function can be called multiple times.
 *------------------------------------------------------------------------*/
static void
ucom_free_unit(struct ucom_super_softc *ssc)
{
	if (!(ssc->sc_flag & UCOM_FLAG_FREE_UNIT))
		return;

	ucom_unit_free(ssc->sc_unit);

	ssc->sc_flag &= ~UCOM_FLAG_FREE_UNIT;
}

/*------------------------------------------------------------------------*
 *	ucom_unref
 *
 * This function will decrement the super UCOM reference count.
 *
 * Return values:
 * 0: UCOM structures are still referenced.
 * Else: UCOM structures are no longer referenced.
 *------------------------------------------------------------------------*/
int
ucom_unref(struct ucom_super_softc *ssc)
{
	int retval;

	lockmgr(&ucom_lock, LK_EXCLUSIVE);
	retval = (ssc->sc_refs < 2);
	ssc->sc_refs--;
	lockmgr(&ucom_lock, LK_RELEASE);

	if (retval)
		ucom_free_unit(ssc);

	return (retval);
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
		DPRINTF("disc_optim: bypass l_rint\n");
		tp->t_state |= TS_CAN_BYPASS_L_RINT;
	} else {
		DPRINTF("disc_optim: can't bypass l_rint\n");
		tp->t_state &= ~TS_CAN_BYPASS_L_RINT;
	}
	sc->hotchar = linesw[tp->t_line].l_hotchar;
}

#if defined(GDB)

#include <gdb/gdb.h>

static gdb_probe_f ucom_gdbprobe;
static gdb_init_f ucom_gdbinit;
static gdb_term_f ucom_gdbterm;
static gdb_getc_f ucom_gdbgetc;
static gdb_putc_f ucom_gdbputc;

GDB_DBGPORT(sio, ucom_gdbprobe, ucom_gdbinit, ucom_gdbterm, ucom_gdbgetc, ucom_gdbputc);

static int
ucom_gdbprobe(void)
{
	return ((ucom_cons_softc != NULL) ? 0 : -1);
}

static void
ucom_gdbinit(void)
{
}

static void
ucom_gdbterm(void)
{
}

static void
ucom_gdbputc(int c)
{
        ucom_cnputc(NULL, c);
}

static int
ucom_gdbgetc(void)
{
        return (ucom_cngetc(NULL));
}

#endif


