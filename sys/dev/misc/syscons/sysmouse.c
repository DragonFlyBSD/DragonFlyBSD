/*-
 * (MPSAFE)
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/syscons/sysmouse.c,v 1.2.2.2 2001/07/16 05:21:24 yokota Exp $
 */

/* MPSAFE NOTE: Take care with locking in sysmouse_event which is called
 *              from syscons.
 */
#include "opt_syscons.h"
#include "opt_evdev.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/event.h>
#include <sys/uio.h>
#include <sys/caps.h>
#include <sys/vnode.h>
#include <sys/kernel.h>
#include <sys/signalvar.h>
#include <sys/filio.h>

#include <machine/console.h>
#include <sys/mouse.h>

#ifdef EVDEV_SUPPORT
#include <dev/misc/evdev/input.h>
#include <dev/misc/evdev/evdev.h>
#endif

#include "syscons.h"

#ifndef SC_NO_SYSMOUSE

#define FIFO_SIZE	256

struct event_fifo {
	mouse_info_t buf[FIFO_SIZE];
	int start;
	int fill;
	unsigned int dropped;
};

struct sysmouse_state {
	struct event_fifo *fifo;
	int level;	/* sysmouse protocol level */
	mousestatus_t syncstatus;
	mousestatus_t readstatus;	/* Only needed for button status */
	int opened;
	int asyncio;
	struct lock sm_lock;
	struct sigio *sm_sigio;
	struct kqinfo rkq;
};

static d_open_t		smopen;
static d_close_t	smclose;
static d_read_t		smread;
static d_ioctl_t	smioctl;
static d_kqfilter_t	smkqfilter;

static struct dev_ops sm_ops = {
	{ "sysmouse", 0, D_MPSAFE },
	.d_open =	smopen,
	.d_close =	smclose,
	.d_read =	smread,
	.d_ioctl =	smioctl,
	.d_kqfilter =	smkqfilter,
};

/* local variables */
static struct sysmouse_state mouse_state;

static int	sysmouse_evtopkt(struct sysmouse_state *sc, mouse_info_t *info,
		    u_char *buf);
static void	smqueue(struct sysmouse_state *sc, mouse_info_t *info);
static int	pktlen(struct sysmouse_state *sc);
static void	smfilter_detach(struct knote *);
static int	smfilter(struct knote *, long);
static void	smget(struct sysmouse_state *sc, mouse_info_t *info);
static void	smpop(struct sysmouse_state *sc);

#ifdef EVDEV_SUPPORT
static struct evdev_dev	*sysmouse_evdev;

static void
smdev_evdev_init(void)
{
	int i;

	sysmouse_evdev = evdev_alloc();
	evdev_set_name(sysmouse_evdev, "System mouse");
	evdev_set_phys(sysmouse_evdev, "sysmouse");
	evdev_set_id(sysmouse_evdev, BUS_VIRTUAL, 0, 0, 0);
	evdev_support_prop(sysmouse_evdev, INPUT_PROP_POINTER);
	evdev_support_event(sysmouse_evdev, EV_SYN);
	evdev_support_event(sysmouse_evdev, EV_REL);
	evdev_support_event(sysmouse_evdev, EV_KEY);
	evdev_support_rel(sysmouse_evdev, REL_X);
	evdev_support_rel(sysmouse_evdev, REL_Y);
	evdev_support_rel(sysmouse_evdev, REL_WHEEL);
	evdev_support_rel(sysmouse_evdev, REL_HWHEEL);
	for (i = 0; i < 8; i++)
		evdev_support_key(sysmouse_evdev, BTN_MOUSE + i);
	if (evdev_register(sysmouse_evdev)) {
		evdev_free(sysmouse_evdev);
		sysmouse_evdev = NULL;
	}
}

static void
smdev_evdev_write(int x, int y, int z, int buttons)
{

	if (sysmouse_evdev == NULL || !(evdev_rcpt_mask & EVDEV_RCPT_SYSMOUSE))
		return;

	evdev_push_event(sysmouse_evdev, EV_REL, REL_X, x);
	evdev_push_event(sysmouse_evdev, EV_REL, REL_Y, y);
	switch (evdev_sysmouse_t_axis) {
	case EVDEV_SYSMOUSE_T_AXIS_PSM:
		switch (z) {
		case 1:
		case -1:
			evdev_push_rel(sysmouse_evdev, REL_WHEEL, -z);
			break;
		case 2:
		case -2:
			evdev_push_rel(sysmouse_evdev, REL_HWHEEL, z / 2);
			break;
		}
		break;
	case EVDEV_SYSMOUSE_T_AXIS_UMS:
		if (buttons & (1 << 6))
			evdev_push_rel(sysmouse_evdev, REL_HWHEEL, 1);
		else if (buttons & (1 << 5))
			evdev_push_rel(sysmouse_evdev, REL_HWHEEL, -1);
		buttons &= ~((1 << 5)|(1 << 6));
		/* PASSTHROUGH */
	case EVDEV_SYSMOUSE_T_AXIS_NONE:
	default:
		evdev_push_rel(sysmouse_evdev, REL_WHEEL, -z);
	}
	evdev_push_mouse_btn(sysmouse_evdev, buttons);
	evdev_sync(sysmouse_evdev);
}
#endif

static int
pktlen(struct sysmouse_state *sc)
{
	if (sc->level == 0)
		return 5;
	else
		return 8;
}

static int
smopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct sysmouse_state *sc = &mouse_state;
	int ret;

	DPRINTF(5, ("smopen: dev:%d,%d\n",
		major(dev), minor(dev)));

	lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
	if (!sc->opened) {
		sc->fifo = kmalloc(sizeof(struct event_fifo),
		    M_SYSCONS, M_WAITOK | M_ZERO);
		sc->opened = 1;
		sc->asyncio = 0;
		sc->sm_sigio = NULL;
		bzero(&sc->readstatus, sizeof(sc->readstatus));
		bzero(&sc->syncstatus, sizeof(sc->syncstatus));
		ret = 0;
	} else {
		ret = EBUSY;
	}
	lockmgr(&sc->sm_lock, LK_RELEASE);

	return ret;
}

static int
smclose(struct dev_close_args *ap)
{
	struct sysmouse_state *sc = &mouse_state;

	lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
	funsetown(&sc->sm_sigio);
	sc->opened = 0;
	sc->asyncio = 0;
	sc->level = 0;
	kfree(sc->fifo, M_SYSCONS);
	sc->fifo = NULL;
	lockmgr(&sc->sm_lock, LK_RELEASE);

	return 0;
}

static int
smread(struct dev_read_args *ap)
{
	struct sysmouse_state *sc = &mouse_state;
	mousestatus_t backupstatus;
	mouse_info_t info;
	u_char buf[8];
	struct uio *uio = ap->a_uio;
	int error = 0, val, cnt = 0;

	lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
	while (sc->fifo->fill <= 0) {
		/* Buffer too small to fit a complete mouse packet */
		if (uio->uio_resid < pktlen(sc)) {
			error = EIO;
			goto done;
		}
		if (ap->a_ioflag & IO_NDELAY) {
			error = EAGAIN;
			goto done;
		}
		error = lksleep(sc, &sc->sm_lock, PCATCH, "smread", 0);
		if (error == EINTR || error == ERESTART) {
			goto done;
		}
	}

	do {
		/* Buffer too small to fit a complete mouse packet */
		if (uio->uio_resid < pktlen(sc)) {
			error = EIO;
			goto done;
		}
		smget(sc, &info);
		backupstatus = sc->readstatus;
		val = sysmouse_evtopkt(sc, &info, buf);
		if (val > 0) {
			error = uiomove(buf, val, uio);
			if (error != 0) {
				sc->readstatus = backupstatus;
				goto done;
			}
			cnt++;
		}
		smpop(sc);
	} while (sc->fifo->fill > 0);

done:
	lockmgr(&sc->sm_lock, LK_RELEASE);
	if (cnt > 0 && error != EFAULT)
		return 0;
	return error;
}

static int
smioctl(struct dev_ioctl_args *ap)
{
	struct sysmouse_state *sc = &mouse_state;
	mousehw_t *hw;
	mousemode_t *mode;

	switch (ap->a_cmd) {
	case FIOSETOWN:
		lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
		fsetown(*(int *)ap->a_data, &sc->sm_sigio);
		lockmgr(&sc->sm_lock, LK_RELEASE);
		return 0;
	case FIOGETOWN:
		lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
		*(int *)ap->a_data = fgetown(&sc->sm_sigio);
		lockmgr(&sc->sm_lock, LK_RELEASE);
		return 0;
	case FIOASYNC:
		lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
		if (*(int *)ap->a_data) {
			sc->asyncio = 1;
		} else {
			sc->asyncio = 0;
		}
		lockmgr(&sc->sm_lock, LK_RELEASE);
		return 0;
	case MOUSE_GETHWINFO:	/* get device information */
		hw = (mousehw_t *)ap->a_data;
		hw->buttons = 10;		/* XXX unknown */
		hw->iftype = MOUSE_IF_SYSMOUSE;
		hw->type = MOUSE_MOUSE;
		hw->model = MOUSE_MODEL_GENERIC;
		hw->hwid = 0;
		return 0;

	case MOUSE_GETMODE:	/* get protocol/mode */
		mode = (mousemode_t *)ap->a_data;
		lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
		mode->level = sc->level;
		lockmgr(&sc->sm_lock, LK_RELEASE);
		switch (mode->level) {
		case 0: /* emulate MouseSystems protocol */
			mode->protocol = MOUSE_PROTO_MSC;
			mode->rate = -1;		/* unknown */
			mode->resolution = -1;	/* unknown */
			mode->accelfactor = 0;	/* disabled */
			mode->packetsize = MOUSE_MSC_PACKETSIZE;
			mode->syncmask[0] = MOUSE_MSC_SYNCMASK;
			mode->syncmask[1] = MOUSE_MSC_SYNC;
			break;

		case 1: /* sysmouse protocol */
			mode->protocol = MOUSE_PROTO_SYSMOUSE;
			mode->rate = -1;
			mode->resolution = -1;
			mode->accelfactor = 0;
			mode->packetsize = MOUSE_SYS_PACKETSIZE;
			mode->syncmask[0] = MOUSE_SYS_SYNCMASK;
			mode->syncmask[1] = MOUSE_SYS_SYNC;
			break;
		}
		return 0;

	case MOUSE_SETMODE:	/* set protocol/mode */
		mode = (mousemode_t *)ap->a_data;
		if (mode->level == -1)
			; 	/* don't change the current setting */
		else if ((mode->level < 0) || (mode->level > 1)) {
			return EINVAL;
		} else {
			lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
			sc->level = mode->level;
			lockmgr(&sc->sm_lock, LK_RELEASE);
		}
		return 0;

	case MOUSE_GETLEVEL:	/* get operation level */
		lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
		*(int *)ap->a_data = sc->level;
		lockmgr(&sc->sm_lock, LK_RELEASE);
		return 0;

	case MOUSE_SETLEVEL:	/* set operation level */
		if ((*(int *)ap->a_data  < 0) || (*(int *)ap->a_data > 1)) {
			return EINVAL;
		}
		lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
		sc->level = *(int *)ap->a_data;
		lockmgr(&sc->sm_lock, LK_RELEASE);
		return 0;

	case MOUSE_GETSTATUS:	/* get accumulated mouse events */
		lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
		*(mousestatus_t *)ap->a_data = sc->syncstatus;
		sc->syncstatus.flags = 0;
		sc->syncstatus.obutton = sc->syncstatus.button;
		sc->syncstatus.dx = 0;
		sc->syncstatus.dy = 0;
		sc->syncstatus.dz = 0;
		lockmgr(&sc->sm_lock, LK_RELEASE);
		return 0;

	case MOUSE_READSTATE:	/* read status from the device */
	case MOUSE_READDATA:	/* read data from the device */
		return ENODEV;
	}

	return ENOTTY;
}

static struct filterops smfiltops =
        { FILTEROP_MPSAFE | FILTEROP_ISFD, NULL, smfilter_detach, smfilter };

static int
smkqfilter(struct dev_kqfilter_args *ap)
{
	struct sysmouse_state *sc = &mouse_state;
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &smfiltops;
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
smfilter_detach(struct knote *kn)
{
	struct sysmouse_state *sc = &mouse_state;
	struct klist *klist;

	klist = &sc->rkq.ki_note;
	knote_remove(klist, kn);
}

static int
smfilter(struct knote *kn, long hint)
{
	struct sysmouse_state *sc = &mouse_state;
	int ready = 0;

	lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
	if (sc->fifo->fill > 0) {
		ready = 1;
		kn->kn_data = 0;
	}
	lockmgr(&sc->sm_lock, LK_RELEASE);

	return ready;
}

static void
smqueue(struct sysmouse_state *sc, mouse_info_t *info)
{
	struct event_fifo *f = sc->fifo;

	if (f->fill >= FIFO_SIZE) {
		f->fill = FIFO_SIZE;
		f->buf[f->start] = *info;
		f->start = (f->start + 1) % FIFO_SIZE;
		f->dropped++;
	} else {
		f->buf[(f->start + f->fill) % FIFO_SIZE] = *info;
		f->fill++;
	}

}

static void
smget(struct sysmouse_state *sc, mouse_info_t *info)
{
	struct event_fifo *f = sc->fifo;

	if (f->fill > 0)
		*info = f->buf[f->start];
}

static void
smpop(struct sysmouse_state *sc)
{
	struct event_fifo *f = sc->fifo;

	if (f->fill > 0) {
		f->fill--;
		f->start = (f->start + 1) % FIFO_SIZE;
	}
}

static void
sm_attach_mouse(void *unused)
{
	struct sysmouse_state *sc = &mouse_state;
	cdev_t dev;

	lockinit(&mouse_state.sm_lock, "sysmouse", 0, LK_CANRECURSE);
	sc->fifo = NULL;

	dev = make_dev(&sm_ops, 0, UID_ROOT, GID_WHEEL, 0600, "sysmouse");
#ifdef EVDEV_SUPPORT
	smdev_evdev_init();
#endif
}

SYSINIT(sysmouse, SI_SUB_DRIVERS, SI_ORDER_ANY, sm_attach_mouse, NULL);

static int
sysmouse_updatestatus(mousestatus_t *status, mouse_info_t *info)
{
	int x, y, z;

	status->obutton = status->button;

	switch (info->operation) {
	case MOUSE_ACTION:
		status->button = info->u.data.buttons;
		/* FALL THROUGH */
	case MOUSE_MOTION_EVENT:
		x = info->u.data.x;
		y = info->u.data.y;
		z = info->u.data.z;
		break;
	case MOUSE_BUTTON_EVENT:
		x = y = z = 0;
		if (info->u.event.value > 0)
			status->button |= info->u.event.id;
		else
			status->button &= ~info->u.event.id;
		break;
	default:
		return 0;
	}

	status->dx += x;
	status->dy += y;
	status->dz += z;
	status->flags |= ((x || y || z) ? MOUSE_POSCHANGED : 0)
			 | (status->obutton ^ status->button);

#ifdef EVDEV_SUPPORT
	smdev_evdev_write(x, y, z, status->button);
#endif

	return 1;
}

/* Requires buf to hold at least 8 bytes, returns number of bytes written */
static int
sysmouse_evtopkt(struct sysmouse_state *sc, mouse_info_t *info, u_char *buf)
{
	/* MOUSE_BUTTON?DOWN -> MOUSE_MSC_BUTTON?UP */
	static int butmap[8] = {
	    MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON2UP | MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON2UP | MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON2UP,
	    MOUSE_MSC_BUTTON2UP,
	    MOUSE_MSC_BUTTON1UP,
	    0,
	};
	int x, y, z;

	sc->readstatus.dx = 0;
	sc->readstatus.dy = 0;
	sc->readstatus.dz = 0;
	sc->readstatus.flags = 0;
	if (sysmouse_updatestatus(&sc->readstatus, info) == 0)
		return 0;

	/* We aren't using the sc->readstatus.dx/dy/dz values */

	if (sc->readstatus.flags == 0)
		return 0;

	x = (info->operation == MOUSE_BUTTON_EVENT ? 0 : info->u.data.x);
	y = (info->operation == MOUSE_BUTTON_EVENT ? 0 : info->u.data.y);
	z = (info->operation == MOUSE_BUTTON_EVENT ? 0 : info->u.data.z);

	/* the first five bytes are compatible with MouseSystems' */
	buf[0] = MOUSE_MSC_SYNC
		 | butmap[sc->readstatus.button & MOUSE_STDBUTTONS];
	x = imax(imin(x, 255), -256);
	buf[1] = x >> 1;
	buf[3] = x - buf[1];
	y = -imax(imin(y, 255), -256);
	buf[2] = y >> 1;
	buf[4] = y - buf[2];
	if (sc->level >= 1) {
		/* extended part */
		z = imax(imin(z, 127), -128);
        	buf[5] = (z >> 1) & 0x7f;
        	buf[6] = (z - (z >> 1)) & 0x7f;
		/* buttons 4-10 */
		buf[7] = (~sc->readstatus.button >> 3) & 0x7f;
	}

	if (sc->level >= 1)
		return 8;

	return 5;
}

int
sysmouse_event(mouse_info_t *info)
{
	struct sysmouse_state *sc = &mouse_state;
	int ret;

	lockmgr(&sc->sm_lock, LK_EXCLUSIVE);
	ret = sysmouse_updatestatus(&sc->syncstatus, info);
	if (ret != 0)
		ret = sc->syncstatus.flags;
	if (!sc->opened) {
		lockmgr(&sc->sm_lock, LK_RELEASE);
		return ret;
	}

	switch (info->operation) {
	case MOUSE_ACTION:
	case MOUSE_MOTION_EVENT:
	case MOUSE_BUTTON_EVENT:
		smqueue(sc, info);
		if (sc->asyncio)
	                pgsigio(sc->sm_sigio, SIGIO, 0);
		lockmgr(&sc->sm_lock, LK_RELEASE);
		wakeup(sc);
		KNOTE(&sc->rkq.ki_note, 0);
		break;
	default:
		lockmgr(&sc->sm_lock, LK_RELEASE);
		break;
	}

	return ret;
}

#endif /* !SC_NO_SYSMOUSE */
