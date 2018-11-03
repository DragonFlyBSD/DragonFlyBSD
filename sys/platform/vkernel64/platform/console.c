/*
 * (MPSAFE)
 *
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
 */

#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/tty.h>
#include <sys/fcntl.h>
#include <sys/signalvar.h>
#include <sys/eventhandler.h>
#include <sys/interrupt.h>
#include <sys/bus.h>
#include <machine/md_var.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

static int console_stolen_by_kernel;
static struct kqueue_info *kqueue_console_info;
static struct tty *kqueue_console_tty;
static int vcons_started;

/************************************************************************
 *			    CONSOLE DEVICE				*
 ************************************************************************
 *
 */

static int vcons_tty_param(struct tty *tp, struct termios *tio);
static void vcons_tty_start(struct tty *tp);
static void vcons_hardintr(void *tpx, struct intrframe *frame __unused);

static d_open_t         vcons_open;
static d_close_t        vcons_close;
static d_ioctl_t        vcons_ioctl;

static struct dev_ops vcons_ops = {
	{ "vcons", 0, D_TTY },
	.d_open =	vcons_open,
	.d_close =	vcons_close,
	.d_read =	ttyread,
	.d_write =	ttywrite,
	.d_ioctl =	vcons_ioctl,
	.d_kqfilter =	ttykqfilter
};

static int
vcons_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;
	int error;

	tp = ttymalloc(&dev->si_tty);
	lwkt_gettoken(&tp->t_token);

#define	ISSET(t, f)	((t) & (f))

	if ((tp->t_state & TS_ISOPEN) == 0) {
		tp->t_oproc = vcons_tty_start;
		tp->t_param = vcons_tty_param;
		tp->t_stop = nottystop;
		tp->t_dev = dev;

		tp->t_state |= TS_CARR_ON | TS_CONNECTED;
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = TTYDEF_SPEED;
		tp->t_ospeed = TTYDEF_SPEED;
		ttsetwater(tp);
	}
	if (minor(dev) == 0) {
		error = (*linesw[tp->t_line].l_open)(dev, tp);
		ioctl(0, TIOCGWINSZ, &tp->t_winsize);

		if (kqueue_console_info == NULL) {
			kqueue_console_tty = tp;
			kqueue_console_info = kqueue_add(0, vcons_hardintr, tp);
		}
	} else {
		/* dummy up other minors so the installer will run */
		error = 0;
	}
	lwkt_reltoken(&tp->t_token);

	return(error);
}

static int
vcons_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;

	tp = dev->si_tty;
	lwkt_gettoken(&tp->t_token);
	(*linesw[tp->t_line].l_close)(tp, ap->a_fflag);
	ttyclose(tp);
	lwkt_reltoken(&tp->t_token);
	return(0);
}

static int
vcons_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;
	int error;

	tp = dev->si_tty;
	lwkt_gettoken(&tp->t_token);
	error = (*linesw[tp->t_line].l_ioctl)(tp, ap->a_cmd, ap->a_data,
					      ap->a_fflag, ap->a_cred);
	if (error != ENOIOCTL) {
		lwkt_reltoken(&tp->t_token);
		return (error);
	}
	error = ttioctl(tp, ap->a_cmd, ap->a_data, ap->a_fflag);
	if (error != ENOIOCTL) {
		lwkt_reltoken(&tp->t_token);
		return (error);
	}
	lwkt_reltoken(&tp->t_token);

	return (ENOTTY);
}

static int
vcons_tty_param(struct tty *tp, struct termios *tio)
{
	lwkt_gettoken(&tp->t_token);
	tp->t_ispeed = tio->c_ispeed;
	tp->t_ospeed = tio->c_ospeed;
	tp->t_cflag = tio->c_cflag;
	lwkt_reltoken(&tp->t_token);
	return(0);
}

static void
vcons_tty_start(struct tty *tp)
{
	int n;
	char buf[64];

	lwkt_gettoken(&tp->t_token);
	if (tp->t_state & (TS_TIMEOUT | TS_TTSTOP)) {
		ttwwakeup(tp);
		lwkt_reltoken(&tp->t_token);
		return;
	}
	tp->t_state |= TS_BUSY;
	while ((n = clist_qtob(&tp->t_outq, buf, sizeof(buf))) > 0) {
		/*
		 * Dummy up ttyv1, etc.
		 */
		if (minor(tp->t_dev) == 0) {
			pwrite(1, buf, n, -1);
		}
	}
	tp->t_state &= ~TS_BUSY;
	lwkt_reltoken(&tp->t_token);
	ttwwakeup(tp);
}

static
void
vcons_hardintr(void *tpx, struct intrframe *frame __unused)
{
	if (console_stolen_by_kernel == 0)
		signalintr(4);
}

/************************************************************************
 *			KERNEL CONSOLE INTERFACE			*
 ************************************************************************
 *
 * Kernel direct-call interface console driver
 */
static cn_probe_t	vconsprobe;
static cn_init_t	vconsinit;
static cn_init_fini_t	vconsinit_fini;
static cn_term_t	vconsterm;
static cn_getc_t	vconsgetc;
static cn_checkc_t	vconscheckc;
static cn_putc_t	vconsputc;

CONS_DRIVER(vcons, vconsprobe, vconsinit, vconsinit_fini, vconsterm, vconsgetc,
		vconscheckc, vconsputc, NULL, NULL);

static struct termios init_tio;
static struct consdev *vconsole;

static void
vconsprobe(struct consdev *cp)
{
	cp->cn_pri = CN_NORMAL;
	cp->cn_probegood = 1;
}

/*
 * This is a little bulky handler to set proper terminal
 * settings in the case of a signal which might lead to
 * termination or suspension.
 */
static void
vconssignal(int sig)
{
	struct termios curtio;
	struct sigaction sa, osa;
	sigset_t ss, oss;

	tcgetattr(0, &curtio);
	tcsetattr(0, TCSAFLUSH, &init_tio);
	bzero(&sa, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_DFL;
	sigaction(sig, &sa, &osa);
	sigemptyset(&ss);
	sigaddset(&ss, sig);
	sigprocmask(SIG_UNBLOCK, &ss, &oss);
	raise(sig);	/* now hand down the sig */
	sigprocmask(SIG_SETMASK, &oss, NULL);
	sigaction(sig, &osa, NULL);
	tcsetattr(0, TCSAFLUSH, &curtio);
}

static void
vconswinchsig(int __unused sig)
{
	signalintr(3);
}

static void
vconswinch_intr(void *arg __unused, void *frame __unused)
{
	struct winsize newsize;

	if (vconsole != NULL && vconsole->cn_dev->si_tty != NULL) {
		ioctl(0, TIOCGWINSZ, &newsize);
		/*
		 * ttioctl(vconsole->cn_dev->si_tty, TIOCSWINSZ, &newsize, 0);
		 * I wished.  Unfortunately this needs a curproc, so do it
		 * manually.
		 */
		if (bcmp(&newsize, &vconsole->cn_dev->si_tty->t_winsize,
			 sizeof(newsize)) != 0) {
			vconsole->cn_dev->si_tty->t_winsize = newsize;
			pgsignal(vconsole->cn_dev->si_tty->t_pgrp, SIGWINCH, 1);
		}
	}
}

/*
 * This has to be an interrupt thread and not a hard interrupt.
 */
static
void
vconsvirt_intr(void *arg __unused, void *frame __unused)
{
	struct tty *tp;
	unsigned char buf[32];
	int i;
	int n;

	if (kqueue_console_info == NULL)
		return;
	tp = kqueue_console_tty;

	lwkt_gettoken(&tp->t_token);
	/*
	 * If we aren't open we only have synchronous traffic via the
	 * debugger and do not need to poll.
	 */
	if ((tp->t_state & TS_ISOPEN) == 0) {
		lwkt_reltoken(&tp->t_token);
		return;
	}

	/*
	 * Only poll if we are open and haven't been stolen by the debugger.
	 */
	if (console_stolen_by_kernel == 0 && (tp->t_state & TS_ISOPEN)) {
		do {
			n = extpread(0, buf, sizeof(buf), O_FNONBLOCKING, -1LL);
			for (i = 0; i < n; ++i)
				(*linesw[tp->t_line].l_rint)(buf[i], tp);
		} while (n > 0);
	}
	lwkt_reltoken(&tp->t_token);
}



static void
vconscleanup(void)
{
	/*
	 * We might catch stray SIGIOs, so try hard.
	 */
	while (tcsetattr(0, TCSAFLUSH, &init_tio) != 0 && errno == EINTR)
		/* NOTHING */;
}

static void
vconsinit(struct consdev *cp)
{
	struct sigaction sa;

	if (vcons_started)
		return;
	vcons_started = 1;
	vconsole = cp;

	tcgetattr(0, &init_tio);
	bzero(&sa, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = vconssignal;
	sigaction(SIGTSTP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	atexit(vconscleanup);
	vcons_set_mode(0);
}

static void
vconsinit_fini(struct consdev *cp)
{
	struct sigaction sa;
	cdev_t dev;
	int i;

	/*
	 * We have to do this here rather then in early boot to be able
	 * to use the interrupt subsystem.
	 */
	register_int_virtual(3, vconswinch_intr, NULL, "swinch", NULL,
			     INTR_MPSAFE);
	register_int_virtual(4, vconsvirt_intr, NULL, "vintr", NULL,
			     INTR_MPSAFE);
	bzero(&sa, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = vconswinchsig;
	sigaction(SIGWINCH, &sa, NULL);

	/*
	 * Implement ttyv0-ttyv7.  At the moment ttyv1-7 are sink nulls.
	 */
	for (i = 0; i < 8; ++i) {
		dev = make_dev(&vcons_ops, i,
			       UID_ROOT, GID_WHEEL, 0600, "ttyv%d", i);
		if (i == 0) {
			cp->cn_dev = dev;
		}
	}
	EVENTHANDLER_REGISTER(shutdown_final, vconscleanup, NULL, SHUTDOWN_PRI_LAST);
}

static void
vconsterm(struct consdev *vp)
{
	vconsole = NULL;
	vconscleanup();
}

static int
vconsgetc(void *private)
{
	unsigned char c;
	ssize_t n;

	console_stolen_by_kernel = 1;
	for (;;) {
		n = pread(0, &c, 1, -1);
		if (n == 1)
			break;
		if (n < 0 && errno == EINTR)
			continue;
		panic("vconsgetc: EOF on console %jd %d", (intmax_t)n, errno);
	}
	console_stolen_by_kernel = 0;
	return((int)c);
}

static int
vconscheckc(void *private)
{
	unsigned char c;

	if (extpread(0, &c, 1, O_FNONBLOCKING, -1LL) == 1)
		return((int)c);
	return(-1);
}

static void
vconsputc(void *private, int c)
{
	char cc = c;

	pwrite(1, &cc, 1, -1);
}

void
vcons_set_mode(int in_debugger)
{
	struct termios tio;

	if (tcgetattr(0, &tio) < 0) {
		return;
	}
	cfmakeraw(&tio);
	tio.c_oflag |= OPOST | ONLCR;
	tio.c_lflag |= ISIG;
	if (in_debugger) {
		tio.c_cc[VINTR] = init_tio.c_cc[VINTR];
		tio.c_cc[VSUSP] = init_tio.c_cc[VSUSP];
		tio.c_cc[VSTATUS] = init_tio.c_cc[VSTATUS];
	} else {
		tio.c_cc[VINTR] = _POSIX_VDISABLE;
		tio.c_cc[VSUSP] = _POSIX_VDISABLE;
		tio.c_cc[VSTATUS] = _POSIX_VDISABLE;
	}
	tcsetattr(0, TCSAFLUSH, &tio);
}
