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

/* MPSAFE NOTE: This file uses the tty_token mostly for the linesw access and
 *		for the internal mouse structures. The latter ones could be protected
 *		by a local lock.
 */
#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/tty.h>
#include <sys/kernel.h>
#include <sys/thread2.h>

#include <machine/console.h>
#include <sys/mouse.h>

#include "syscons.h"

#ifndef SC_NO_SYSMOUSE

#define	CDEV_MAJOR	12		/* major number, shared with syscons */
#define SC_MOUSE 	128		/* minor number */

static d_open_t		smopen;
static d_close_t	smclose;
static d_ioctl_t	smioctl;

static struct dev_ops sm_ops = {
	{ "sysmouse", 0, D_TTY },
	.d_open =	smopen,
	.d_close =	smclose,
	.d_read =	ttyread,
	.d_ioctl =	smioctl,
	.d_kqfilter =	ttykqfilter,
	.d_revoke =	ttyrevoke
};

/* local variables */
static struct tty	*sysmouse_tty;
static int		mouse_level;	/* sysmouse protocol level */
static mousestatus_t	mouse_status;

static void		smstart(struct tty *tp);
static int		smparam(struct tty *tp, struct termios *t);

static int
smopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;
	int ret;

	DPRINTF(5, ("smopen: dev:%d,%d, vty:%d\n",
		major(dev), minor(dev), SC_VTY(dev)));

#if 0
	if (SC_VTY(dev) != SC_MOUSE)
		return ENXIO;
#endif

	lwkt_gettoken(&tty_token);
	tp = dev->si_tty = ttymalloc(dev->si_tty);
	if (!(tp->t_state & TS_ISOPEN)) {
		sysmouse_tty = tp;
		tp->t_oproc = smstart;
		tp->t_param = smparam;
		tp->t_stop = nottystop;
		tp->t_dev = dev;
		ttychars(tp);
		tp->t_iflag = 0;
		tp->t_oflag = 0;
		tp->t_lflag = 0;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
		smparam(tp, &tp->t_termios);
		(*linesw[tp->t_line].l_modem)(tp, 1);
	} else if (tp->t_state & TS_XCLUDE && priv_check_cred(ap->a_cred, PRIV_ROOT, 0)) {
		lwkt_reltoken(&tty_token);
		return EBUSY;
	}

	ret = (*linesw[tp->t_line].l_open)(dev, tp);
	lwkt_reltoken(&tty_token);
	return ret;
}

static int
smclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;

	tp = dev->si_tty;
	crit_enter();
	lwkt_gettoken(&tty_token);
	mouse_level = 0;
	(*linesw[tp->t_line].l_close)(tp, ap->a_fflag);
	ttyclose(tp);
	lwkt_reltoken(&tty_token);
	crit_exit();

	return 0;
}

static void
smstart(struct tty *tp)
{
	struct clist *rbp;
	u_char buf[PCBURST];

	crit_enter();
	lwkt_gettoken(&tty_token);
	if (!(tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP))) {
		tp->t_state |= TS_BUSY;
		rbp = &tp->t_outq;
		while (rbp->c_cc)
			q_to_b(rbp, buf, PCBURST);
		tp->t_state &= ~TS_BUSY;
		ttwwakeup(tp);
	}
	lwkt_reltoken(&tty_token);
	crit_exit();
}

static int
smparam(struct tty *tp, struct termios *t)
{
	lwkt_gettoken(&tty_token);
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;
	lwkt_reltoken(&tty_token);
	return 0;
}

static int
smioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;
	mousehw_t *hw;
	mousemode_t *mode;
	int error;

	lwkt_gettoken(&tty_token);
	tp = dev->si_tty;

	switch (ap->a_cmd) {
	case MOUSE_GETHWINFO:	/* get device information */
		hw = (mousehw_t *)ap->a_data;
		hw->buttons = 10;		/* XXX unknown */
		hw->iftype = MOUSE_IF_SYSMOUSE;
		hw->type = MOUSE_MOUSE;
		hw->model = MOUSE_MODEL_GENERIC;
		hw->hwid = 0;
		lwkt_reltoken(&tty_token);
		return 0;

	case MOUSE_GETMODE:	/* get protocol/mode */
		mode = (mousemode_t *)ap->a_data;
		mode->level = mouse_level;
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
		lwkt_reltoken(&tty_token);
		return 0;

	case MOUSE_SETMODE:	/* set protocol/mode */
		mode = (mousemode_t *)ap->a_data;
		if (mode->level == -1)
			; 	/* don't change the current setting */
		else if ((mode->level < 0) || (mode->level > 1)) {
			lwkt_reltoken(&tty_token);
			return EINVAL;
		} else {
			mouse_level = mode->level;
		}
		lwkt_reltoken(&tty_token);
		return 0;

	case MOUSE_GETLEVEL:	/* get operation level */
		*(int *)ap->a_data = mouse_level;
		lwkt_reltoken(&tty_token);
		return 0;

	case MOUSE_SETLEVEL:	/* set operation level */
		if ((*(int *)ap->a_data  < 0) || (*(int *)ap->a_data > 1)) {
			lwkt_reltoken(&tty_token);
			return EINVAL;
		}
		mouse_level = *(int *)ap->a_data;
		lwkt_reltoken(&tty_token);
		return 0;

	case MOUSE_GETSTATUS:	/* get accumulated mouse events */
		crit_enter();
		*(mousestatus_t *)ap->a_data = mouse_status;
		mouse_status.flags = 0;
		mouse_status.obutton = mouse_status.button;
		mouse_status.dx = 0;
		mouse_status.dy = 0;
		mouse_status.dz = 0;
		crit_exit();
		lwkt_reltoken(&tty_token);
		return 0;

#if 0 /* notyet */
	case MOUSE_GETVARS:	/* get internal mouse variables */
	case MOUSE_SETVARS:	/* set internal mouse variables */
		lwkt_reltoken(&tty_token);
		return ENODEV;
#endif

	case MOUSE_READSTATE:	/* read status from the device */
	case MOUSE_READDATA:	/* read data from the device */
		lwkt_reltoken(&tty_token);
		return ENODEV;
	}

	error = (*linesw[tp->t_line].l_ioctl)(tp, ap->a_cmd, ap->a_data, ap->a_fflag, ap->a_cred);
	if (error != ENOIOCTL) {
		lwkt_reltoken(&tty_token);
		return error;
	}
	error = ttioctl(tp, ap->a_cmd, ap->a_data, ap->a_fflag);
	if (error != ENOIOCTL) {
		lwkt_reltoken(&tty_token);
		return error;
	}
	lwkt_reltoken(&tty_token);
	return ENOTTY;
}

static void
sm_attach_mouse(void *unused)
{
	cdev_t dev;

	dev = make_dev(&sm_ops, SC_MOUSE, UID_ROOT, GID_WHEEL,
		       0600, "sysmouse");
	/* sysmouse doesn't have scr_stat */
}

SYSINIT(sysmouse, SI_SUB_DRIVERS, SI_ORDER_MIDDLE + CDEV_MAJOR,
	sm_attach_mouse, NULL);

int
sysmouse_event(mouse_info_t *info)
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
	u_char buf[8];
	int x, y, z;
	int i;

	lwkt_gettoken(&tty_token);
	switch (info->operation) {
	case MOUSE_ACTION:
        	mouse_status.button = info->u.data.buttons;
		/* FALL THROUGH */
	case MOUSE_MOTION_EVENT:
		x = info->u.data.x;
		y = info->u.data.y;
		z = info->u.data.z;
		break;
	case MOUSE_BUTTON_EVENT:
		x = y = z = 0;
		if (info->u.event.value > 0)
			mouse_status.button |= info->u.event.id;
		else
			mouse_status.button &= ~info->u.event.id;
		break;
	default:
		lwkt_reltoken(&tty_token);
		return 0;
	}

	mouse_status.dx += x;
	mouse_status.dy += y;
	mouse_status.dz += z;
	mouse_status.flags |= ((x || y || z) ? MOUSE_POSCHANGED : 0)
			      | (mouse_status.obutton ^ mouse_status.button);
	if (mouse_status.flags == 0) {
		lwkt_reltoken(&tty_token);
		return 0;
	}

	if ((sysmouse_tty == NULL) || !(sysmouse_tty->t_state & TS_ISOPEN)) {
		lwkt_reltoken(&tty_token);
		return mouse_status.flags;
	}

	/* the first five bytes are compatible with MouseSystems' */
	buf[0] = MOUSE_MSC_SYNC
		 | butmap[mouse_status.button & MOUSE_STDBUTTONS];
	x = imax(imin(x, 255), -256);
	buf[1] = x >> 1;
	buf[3] = x - buf[1];
	y = -imax(imin(y, 255), -256);
	buf[2] = y >> 1;
	buf[4] = y - buf[2];
	for (i = 0; i < MOUSE_MSC_PACKETSIZE; ++i)
		(*linesw[sysmouse_tty->t_line].l_rint)(buf[i], sysmouse_tty);
	if (mouse_level >= 1) {
		/* extended part */
        	z = imax(imin(z, 127), -128);
        	buf[5] = (z >> 1) & 0x7f;
        	buf[6] = (z - (z >> 1)) & 0x7f;
        	/* buttons 4-10 */
        	buf[7] = (~mouse_status.button >> 3) & 0x7f;
        	for (i = MOUSE_MSC_PACKETSIZE; i < MOUSE_SYS_PACKETSIZE; ++i)
			(*linesw[sysmouse_tty->t_line].l_rint)(buf[i],
							       sysmouse_tty);
	}

	lwkt_reltoken(&tty_token);
	return mouse_status.flags;
}

#endif /* !SC_NO_SYSMOUSE */
