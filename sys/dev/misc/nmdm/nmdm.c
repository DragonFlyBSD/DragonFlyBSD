/*
 * (MPSAFE)
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/nmdm/nmdm.c,v 1.5.2.1 2001/08/11 00:54:14 mp Exp $
 */

/*
 * MPSAFE NOTE: This file acquires the tty_token mainly for linesw access and
 *		tp (struct tty) access.
 */

/*
 * Pseudo-nulmodem Driver
 */
#include "opt_compat.h"
#include <sys/param.h>
#include <sys/systm.h>
#if defined(COMPAT_43)
#include <sys/ioctl_compat.h>
#endif
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/thread2.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>

MALLOC_DEFINE(M_NLMDM, "nullmodem", "nullmodem data structures");

static void nmdmstart (struct tty *tp);
static void nmdmstop (struct tty *tp, int rw);
static void wakeup_other (struct tty *tp, int flag);
static void nmdminit (int n);

static	d_open_t	nmdmopen;
static	d_close_t	nmdmclose;
static	d_read_t	nmdmread;
static	d_write_t	nmdmwrite;
static	d_ioctl_t	nmdmioctl;

#define	CDEV_MAJOR	18
static struct dev_ops nmdm_ops = {
	{ "pts", 0, D_TTY },
	.d_open =	nmdmopen,
	.d_close =	nmdmclose,
	.d_read =	nmdmread,
	.d_write =	nmdmwrite,
	.d_ioctl =	nmdmioctl,
	.d_kqfilter = 	ttykqfilter,
	.d_revoke =	ttyrevoke
};

#define BUFSIZ 100		/* Chunk size iomoved to/from user */

struct softpart {
	struct tty nm_tty;
	cdev_t	dev;
	int	modemsignals;	/* bits defined in sys/ttycom.h */
	int	gotbreak;
};

struct	nm_softc {
	int	pt_flags;
	struct softpart part1, part2;
	struct	prison *pt_prison;
};

#define	PF_STOPPED	0x10		/* user told stopped */

static void
nmdm_crossover(struct nm_softc *pti,
		struct softpart *ourpart,
		struct softpart *otherpart);

#define GETPARTS(tp, ourpart, otherpart) \
do {	\
	struct nm_softc *pti = tp->t_dev->si_drv1; \
	if (tp == &pti->part1.nm_tty) { \
		ourpart = &pti->part1; \
		otherpart = &pti->part2; \
	} else { \
		ourpart = &pti->part2; \
		otherpart = &pti->part1; \
	}  \
} while (0)

/*
 * This function creates and initializes a pair of ttys.
 *
 * NOTE: Must be called with tty_token held
 */
static void
nmdminit(int n)
{
	cdev_t dev1, dev2;
	struct nm_softc *pt;

	/*
	 * Simplified unit number, use low 8 bits of minor number
	 * (remember, the minor number mask is 0xffff00ff).
	 */
	if (n & ~0x7f)
		return;

	ASSERT_LWKT_TOKEN_HELD(&tty_token);

	pt = kmalloc(sizeof(*pt), M_NLMDM, M_WAITOK | M_ZERO);
	pt->part1.dev = dev1 = make_dev(&nmdm_ops, n << 1,
					0, 0, 0666, "nmdm%dA", n);
	pt->part2.dev = dev2 = make_dev(&nmdm_ops, (n << 1) + 1,
					0, 0, 0666, "nmdm%dB", n);

	dev1->si_drv1 = dev2->si_drv1 = pt;
	dev1->si_tty = &pt->part1.nm_tty;
	dev2->si_tty = &pt->part2.nm_tty;
	ttyregister(&pt->part1.nm_tty);
	ttyregister(&pt->part2.nm_tty);
	pt->part1.nm_tty.t_oproc = nmdmstart;
	pt->part2.nm_tty.t_oproc = nmdmstart;
	pt->part1.nm_tty.t_stop = nmdmstop;
	pt->part2.nm_tty.t_dev = dev1;
	pt->part1.nm_tty.t_dev = dev2;
	pt->part2.nm_tty.t_stop = nmdmstop;
}

/*ARGSUSED*/
static	int
nmdmopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp, *tp2;
	int error;
	int minr;
#if 0
	cdev_t nextdev;
#endif
	struct nm_softc *pti;
	int is_b;
	int	pair;
	struct	softpart *ourpart, *otherpart;

	minr = lminor(dev);
	pair = minr >> 1;
	is_b = minr & 1;
	
#if 0
	/*
	 * XXX: Gross hack for DEVFS:
	 * If we openned this device, ensure we have the
	 * next one too, so people can open it.
	 */
	if (pair < 127) {
		nextdev = makedev(major(dev), (pair+pair) + 1);
		if (!nextdev->si_drv1) {
			nmdminit(pair + 1);
		}
	}
#endif
	if (!dev->si_drv1)
		nmdminit(pair);

	if (!dev->si_drv1)
		return(ENXIO);	

	lwkt_gettoken(&tty_token);
	pti = dev->si_drv1;
	if (is_b) 
		tp = &pti->part2.nm_tty;
	else 
		tp = &pti->part1.nm_tty;
	GETPARTS(tp, ourpart, otherpart);
	tp2 = &otherpart->nm_tty;
	ourpart->modemsignals |= TIOCM_LE;

	if ((tp->t_state & TS_ISOPEN) == 0) {
		ttychars(tp);		/* Set up default chars */
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
	} else if (tp->t_state & TS_XCLUDE && priv_check_cred(ap->a_cred, PRIV_ROOT, 0)) {
		lwkt_reltoken(&tty_token);
		return (EBUSY);
	} else if (pti->pt_prison != ap->a_cred->cr_prison) {
		lwkt_reltoken(&tty_token);
		return (EBUSY);
	}

	/*
	 * If the other side is open we have carrier
	 */
	if (tp2->t_state & TS_ISOPEN) {
		(void)(*linesw[tp->t_line].l_modem)(tp, 1);
	}

	/*
	 * And the other side gets carrier as we are now open.
	 */
	(void)(*linesw[tp2->t_line].l_modem)(tp2, 1);

	/* External processing makes no sense here */
	tp->t_lflag &= ~EXTPROC;

	/* 
	 * Wait here if we don't have carrier.
	 */
#if 0
	while ((tp->t_state & TS_CARR_ON) == 0) {
		if (flag & FNONBLOCK)
			break;
		error = ttysleep(tp, TSA_CARR_ON(tp), PCATCH, "nmdopn", 0);
		if (error) {
			lwkt_reltoken(&tty_token);
			return (error);
		}
	}
#endif

	/*
	 * Give the line disciplin a chance to set this end up.
	 */
	error = (*linesw[tp->t_line].l_open)(dev, tp);

	/*
	 * Wake up the other side.
	 * Theoretically not needed.
	 */
	ourpart->modemsignals |= TIOCM_DTR;
	nmdm_crossover(pti, ourpart, otherpart);
	if (error == 0)
		wakeup_other(tp, FREAD|FWRITE); /* XXX */
	lwkt_reltoken(&tty_token);
	return (error);
}

static int
nmdmclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp, *tp2;
	int err;
	struct softpart *ourpart, *otherpart;

	lwkt_gettoken(&tty_token);
	/*
	 * let the other end know that the game is up
	 */
	tp = dev->si_tty;
	GETPARTS(tp, ourpart, otherpart);
	tp2 = &otherpart->nm_tty;
	(void)(*linesw[tp2->t_line].l_modem)(tp2, 0);

	/*
	 * XXX MDMBUF makes no sense for nmdms but would inhibit the above
	 * l_modem().  CLOCAL makes sense but isn't supported.   Special
	 * l_modem()s that ignore carrier drop make no sense for nmdms but
	 * may be in use because other parts of the line discipline make
	 * sense for nmdms.  Recover by doing everything that a normal
	 * ttymodem() would have done except for sending a SIGHUP.
	 */
	if (tp2->t_state & TS_ISOPEN) {
		tp2->t_state &= ~(TS_CARR_ON | TS_CONNECTED);
		tp2->t_state |= TS_ZOMBIE;
		ttyflush(tp2, FREAD | FWRITE);
	}

	err = (*linesw[tp->t_line].l_close)(tp, ap->a_fflag);
	ourpart->modemsignals &= ~TIOCM_DTR;
	nmdm_crossover(dev->si_drv1, ourpart, otherpart);
	nmdmstop(tp, FREAD|FWRITE);
	(void) ttyclose(tp);
	lwkt_reltoken(&tty_token);
	return (err);
}

static int
nmdmread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int error = 0;
	struct tty *tp;
#if 0
	struct tty *tp2;
	struct softpart *ourpart, *otherpart;
#endif

	lwkt_gettoken(&tty_token);
	tp = dev->si_tty;
#if 0
	GETPARTS(tp, ourpart, otherpart);
	tp2 = &otherpart->nm_tty;

	if (tp2->t_state & TS_ISOPEN) {
		error = (*linesw[tp->t_line].l_read)(tp, ap->a_uio, flag);
		wakeup_other(tp, FWRITE);
	} else {
		if (flag & IO_NDELAY) {
			lwkt_reltoken(&tty_token);
			return (EWOULDBLOCK);
		}
		error = tsleep(TSA_PTC_READ(tp), PCATCH, "nmdout", 0);
		}
	}
#else
	if ((error = (*linesw[tp->t_line].l_read)(tp, ap->a_uio, ap->a_ioflag)) == 0)
		wakeup_other(tp, FWRITE);
#endif
	lwkt_reltoken(&tty_token);
	return (error);
}

/*
 * Write to pseudo-tty.
 * Wakeups of controlling tty will happen
 * indirectly, when tty driver calls nmdmstart.
 */
static	int
nmdmwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	u_char *cp = NULL;
	size_t cc = 0;
	u_char locbuf[BUFSIZ];
	int cnt = 0;
	int error = 0;
	struct tty *tp1, *tp;
	struct softpart *ourpart, *otherpart;

	lwkt_gettoken(&tty_token);
	tp1 = dev->si_tty;
	/*
	 * Get the other tty struct.
	 * basically we are writing into the INPUT side of the other device.
	 */
	GETPARTS(tp1, ourpart, otherpart);
	tp = &otherpart->nm_tty;

again:
	if ((tp->t_state & TS_ISOPEN) == 0) {
		lwkt_reltoken(&tty_token);
		return (EIO);
	}
	while (uio->uio_resid > 0 || cc > 0) {
		/*
		 * Fill up the buffer if it's empty
		 */
		if (cc == 0) {
			cc = szmin(uio->uio_resid, BUFSIZ);
			cp = locbuf;
			error = uiomove((caddr_t)cp, cc, uio);
			if (error) {
				lwkt_reltoken(&tty_token);
				return (error);
			}
			/* check again for safety */
			if ((tp->t_state & TS_ISOPEN) == 0) {
				/* adjust for data copied in but not written */
				uio->uio_resid += cc;
				lwkt_reltoken(&tty_token);
				return (EIO);
			}
		}
		while (cc > 0) {
			if (((tp->t_rawq.c_cc + tp->t_canq.c_cc) >= (TTYHOG-2))
			&& ((tp->t_canq.c_cc > 0) || !(tp->t_iflag&ICANON))) {
				/*
	 			 * Come here to wait for space in outq,
				 * or space in rawq, or an empty canq.
	 			 */
				wakeup(TSA_HUP_OR_INPUT(tp));
				if ((tp->t_state & TS_CONNECTED) == 0) {
					/*
					 * Data piled up because not connected.
					 * Adjust for data copied in but
					 * not written.
					 */
					uio->uio_resid += cc;
					lwkt_reltoken(&tty_token);
					return (EIO);
				}
				if (ap->a_ioflag & IO_NDELAY) {
					/*
				         * Don't wait if asked not to.
					 * Adjust for data copied in but
					 * not written.
					 */
					uio->uio_resid += cc;
					if (cnt == 0) {
						lwkt_reltoken(&tty_token);
						return (EWOULDBLOCK);
					}
					lwkt_reltoken(&tty_token);
					return (0);
				}
				error = tsleep(TSA_PTC_WRITE(tp),
						PCATCH, "nmdout", 0);
				if (error) {
					/*
					 * Tsleep returned (signal?).
					 * Go find out what the user wants.
					 * adjust for data copied in but
					 * not written
					 */
					uio->uio_resid += cc;
					lwkt_reltoken(&tty_token);
					return (error);
				}
				goto again;
			}
			(*linesw[tp->t_line].l_rint)(*cp++, tp);
			cnt++;
			cc--;
		}
		cc = 0;
	}
	lwkt_reltoken(&tty_token);
	return (0);
}

/*
 * Start output on pseudo-tty.
 * Wake up process selecting or sleeping for input from controlling tty.
 */
static void
nmdmstart(struct tty *tp)
{
	struct nm_softc *pti = tp->t_dev->si_drv1;

	lwkt_gettoken(&tty_token);
	if (tp->t_state & TS_TTSTOP) {
		lwkt_reltoken(&tty_token);
		return;
	}
	pti->pt_flags &= ~PF_STOPPED;
	wakeup_other(tp, FREAD);
	lwkt_reltoken(&tty_token);
}

/* Wakes up the OTHER tty;*/
static void
wakeup_other(struct tty *tp, int flag)
{
	struct softpart *ourpart, *otherpart;

	lwkt_gettoken(&tty_token);
	GETPARTS(tp, ourpart, otherpart);
	if (flag & FREAD) {
		wakeup(TSA_PTC_READ((&otherpart->nm_tty)));
		KNOTE(&otherpart->nm_tty.t_rkq.ki_note, 0);
	}
	if (flag & FWRITE) {
		wakeup(TSA_PTC_WRITE((&otherpart->nm_tty)));
		KNOTE(&otherpart->nm_tty.t_wkq.ki_note, 0);
	}
	lwkt_reltoken(&tty_token);
}

static	void
nmdmstop(struct tty *tp, int flush)
{
	struct nm_softc *pti = tp->t_dev->si_drv1;
	int flag;

	lwkt_gettoken(&tty_token);
	/* note: FLUSHREAD and FLUSHWRITE already ok */
	if (flush == 0) {
		flush = TIOCPKT_STOP;
		pti->pt_flags |= PF_STOPPED;
	} else
		pti->pt_flags &= ~PF_STOPPED;
	/* change of perspective */
	flag = 0;
	if (flush & FREAD)
		flag |= FWRITE;
	if (flush & FWRITE)
		flag |= FREAD;
	wakeup_other(tp, flag);
	lwkt_reltoken(&tty_token);
}

/*ARGSUSED*/
static	int
nmdmioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp = dev->si_tty;
	struct nm_softc *pti = dev->si_drv1;
	int error;
	struct softpart *ourpart, *otherpart;

	crit_enter();
	lwkt_gettoken(&tty_token);
	GETPARTS(tp, ourpart, otherpart);

	error = (*linesw[tp->t_line].l_ioctl)(tp, ap->a_cmd, ap->a_data,
					      ap->a_fflag, ap->a_cred);
	if (error == ENOIOCTL)
		 error = ttioctl(tp, ap->a_cmd, ap->a_data, ap->a_fflag);
	if (error == ENOIOCTL) {
		switch (ap->a_cmd) {
		case TIOCSBRK:
			otherpart->gotbreak = 1;
			break;
		case TIOCCBRK:
			break;
		case TIOCSDTR:
			ourpart->modemsignals |= TIOCM_DTR;
			break;
		case TIOCCDTR:
			ourpart->modemsignals &= TIOCM_DTR;
			break;
		case TIOCMSET:
			ourpart->modemsignals = *(int *)ap->a_data;
			otherpart->modemsignals = *(int *)ap->a_data;
			break;
		case TIOCMBIS:
			ourpart->modemsignals |= *(int *)ap->a_data;
			break;
		case TIOCMBIC:
			ourpart->modemsignals &= ~(*(int *)ap->a_data);
			otherpart->modemsignals &= ~(*(int *)ap->a_data);
			break;
		case TIOCMGET:
			*(int *)ap->a_data = ourpart->modemsignals;
			break;
		case TIOCMSDTRWAIT:
			break;
		case TIOCMGDTRWAIT:
			*(int *)ap->a_data = 0;
			break;
		case TIOCTIMESTAMP:
		case TIOCDCDTIMESTAMP:
		default:
			lwkt_reltoken(&tty_token);
			crit_exit();
			error = ENOTTY;
			return (error);
		}
		error = 0;
		nmdm_crossover(pti, ourpart, otherpart);
	}
	lwkt_reltoken(&tty_token);
	crit_exit();
	return (error);
}

static void
nmdm_crossover(struct nm_softc *pti,
		struct softpart *ourpart,
		struct softpart *otherpart)
{
	lwkt_gettoken(&tty_token);
	otherpart->modemsignals &= ~(TIOCM_CTS|TIOCM_CAR);
	if (ourpart->modemsignals & TIOCM_RTS)
		otherpart->modemsignals |= TIOCM_CTS;
	if (ourpart->modemsignals & TIOCM_DTR)
		otherpart->modemsignals |= TIOCM_CAR;
	lwkt_reltoken(&tty_token);
}



static void nmdm_drvinit (void *unused);

static void
nmdm_drvinit(void *unused)
{
	/* XXX: Gross hack for DEVFS */
	lwkt_gettoken(&tty_token);
	nmdminit(0);
	lwkt_reltoken(&tty_token);
}

SYSINIT(nmdmdev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE + CDEV_MAJOR, nmdm_drvinit,
    NULL);
