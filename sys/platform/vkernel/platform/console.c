/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/platform/vkernel/platform/console.c,v 1.6 2007/01/08 21:41:58 dillon Exp $
 */

#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/tty.h>
#include <sys/termios.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <termios.h>

static int console_stolen_by_kernel;
static struct callout console_callout;

/*
 * Global console locking functions
 */
void
cons_lock(void)
{
}

void
cons_unlock(void)
{
}

/************************************************************************
 *			    CONSOLE DEVICE				*
 ************************************************************************
 *
 */

#define CDEV_MAJOR	183

static int vcons_tty_param(struct tty *tp, struct termios *tio);
static void vcons_tty_start(struct tty *tp);
static void vcons_intr(void *tpx);

static d_open_t         vcons_open;
static d_close_t        vcons_close;
static d_ioctl_t        vcons_ioctl;

static struct dev_ops vcons_ops = {
	{ "vcons", CDEV_MAJOR, D_TTY },
	.d_open =	vcons_open,
	.d_close =	vcons_close,
	.d_read =	ttyread,
	.d_write =	ttywrite,
	.d_ioctl =	vcons_ioctl,
	.d_poll =	ttypoll,
};

static int
vcons_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;
	int error;

	if (minor(dev) != 255)
		return(ENXIO);

	tp = dev->si_tty = ttymalloc(dev->si_tty);
	tp->t_oproc = vcons_tty_start;
	tp->t_param = vcons_tty_param;
	tp->t_stop = nottystop;
	tp->t_dev = dev;

#if 0
	if (tp->t_state & TS_ISOPEN)
		return (EBUSY);
#endif

	tp->t_state |= TS_CARR_ON | TS_CONNECTED;
	ttychars(tp);
	tp->t_iflag = TTYDEF_IFLAG;
	tp->t_oflag = TTYDEF_OFLAG;
	tp->t_cflag = TTYDEF_CFLAG;
	tp->t_lflag = TTYDEF_LFLAG;
	tp->t_ispeed = TTYDEF_SPEED;
	tp->t_ospeed = TTYDEF_SPEED;
	ttsetwater(tp);

	error = (*linesw[tp->t_line].l_open)(dev, tp);
	if (error == 0) {
		callout_init(&console_callout);
		callout_reset(&console_callout, hz / 30 + 1, vcons_intr, tp);
	}
	return(error);
}

static int
vcons_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;

	if (minor(dev) != 255)
		return(ENXIO);
	tp = dev->si_tty;
	if (tp->t_state & TS_ISOPEN) {
		callout_stop(&console_callout);
		(*linesw[tp->t_line].l_close)(tp, ap->a_fflag);
		ttyclose(tp);
	}
	return(0);
}

static int
vcons_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;
	int error;

	if (minor(dev) != 255)
		return(ENXIO);
	tp = dev->si_tty;
	error = (*linesw[tp->t_line].l_ioctl)(tp, ap->a_cmd, ap->a_data,
					      ap->a_fflag, ap->a_cred);
	if (error != ENOIOCTL)
		return (error);
	error = ttioctl(tp, ap->a_cmd, ap->a_data, ap->a_fflag);
	if (error != ENOIOCTL)
		return (error);
	return (ENOTTY);
}

static int
vcons_tty_param(struct tty *tp, struct termios *tio)
{
	tp->t_ispeed = tio->c_ispeed;
	tp->t_ospeed = tio->c_ospeed;
	tp->t_cflag = tio->c_cflag;
	return(0);
}

static void
vcons_tty_start(struct tty *tp)
{
	int n;
	char buf[64];

	if (tp->t_state & (TS_TIMEOUT | TS_TTSTOP)) {
		ttwwakeup(tp);
		return;
	}
	tp->t_state |= TS_BUSY;
	while ((n = q_to_b(&tp->t_outq, buf, sizeof(buf))) > 0)
		write(1, buf, n);
	tp->t_state &= ~TS_BUSY;
	ttwwakeup(tp);
}

static
void
vcons_intr(void *tpx)
{
	struct tty *tp = tpx;
	unsigned char buf[32];
	int i;
	int n;

	if (console_stolen_by_kernel == 0 && (tp->t_state & TS_ISOPEN)) {
		do {
			n = extpread(0, buf, sizeof(buf), O_FNONBLOCKING, -1LL);
			for (i = 0; i < n; ++i)
				(*linesw[tp->t_line].l_rint)(buf[i], tp);
		} while (n > 0);
	}
	callout_reset(&console_callout, hz / 30 + 1, vcons_intr, tp);
}

/************************************************************************
 *			KERNEL CONSOLE INTERFACE			*
 ************************************************************************
 *
 * Kernel direct-call interface console driver
 */
static cn_probe_t	vconsprobe;
static cn_init_t	vconsinit;
static cn_term_t	vconsterm;
static cn_getc_t	vconsgetc;
static cn_checkc_t	vconscheckc;
static cn_putc_t	vconsputc;

CONS_DRIVER(vcons, vconsprobe, vconsinit, vconsterm, vconsgetc, 
		vconscheckc, vconsputc, NULL);

static void
vconsprobe(struct consdev *cp)
{
	struct termios tio;

	cp->cn_pri = CN_NORMAL;
	cp->cn_dev = make_dev(&vcons_ops, 255,
			      UID_ROOT, GID_WHEEL, 0600, "vconsolectl");

	if (tcgetattr(0, &tio) == 0) {
		cfmakeraw(&tio);
		tio.c_lflag |= ISIG;
		tcsetattr(0, TCSAFLUSH, &tio);
	}
}

static void
vconsinit(struct consdev *cp)
{
}

static void
vconsterm(struct consdev *vp)
{
}

static int
vconsgetc(cdev_t dev)
{
	unsigned char c;
	ssize_t n;

	console_stolen_by_kernel = 1;
	for (;;) {
		if ((n = read(0, &c, 1)) == 1)
			break;
		if (n < 0 && errno == EINTR)
			continue;
		panic("vconsgetc: EOF on console %d %d", n ,errno);
	}
	console_stolen_by_kernel = 0;
	return((int)c);
}

static int
vconscheckc(cdev_t dev)
{
	unsigned char c;

	if (extpread(0, &c, 1, O_FNONBLOCKING, -1LL) == 1)
		return((int)c);
	return(-1);
}

static void
vconsputc(cdev_t dev, int c)
{
	char cc = c;

	write(1, &cc, 1);
}


