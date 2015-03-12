/*
 * Copyright (c) 1982, 1986, 1993
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
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)subr_log.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/kern/subr_log.c,v 1.39.2.2 2001/06/02 08:11:25 phk Exp $
 */

/*
 * Error log buffer for kernel printf's.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/msgbuf.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/event.h>
#include <sys/filedesc.h>
#include <sys/sysctl.h>
#include <sys/thread2.h>

#define LOG_ASYNC	0x04
#define LOG_RDWAIT	0x08

static	d_open_t	logopen;
static	d_close_t	logclose;
static	d_read_t	logread;
static	d_ioctl_t	logioctl;
static	d_kqfilter_t	logkqfilter;

static	void logtimeout(void *arg);
static	void logfiltdetach(struct knote *kn);
static	int  logfiltread(struct knote *kn, long hint);

#define CDEV_MAJOR 7
static struct dev_ops log_ops = {
	{ "log", 0, 0 },
	.d_open =	logopen,
	.d_close =	logclose,
	.d_read =	logread,
	.d_ioctl =	logioctl,
	.d_kqfilter =	logkqfilter
};

static struct logsoftc {
	int	sc_state;		/* see above for possibilities */
	struct	kqinfo	sc_kqp;		/* processes waiting on I/O */
	struct  sigio *sc_sigio;	/* information for async I/O */
	struct	callout sc_callout;	/* callout to wakeup syslog  */
} logsoftc;

int	log_open;			/* also used in log() */

/* Times per second to check for a pending syslog wakeup. */
static int	log_wakeups_per_second = 5;
SYSCTL_INT(_kern, OID_AUTO, log_wakeups_per_second, CTLFLAG_RW,
    &log_wakeups_per_second, 0, "");

/*ARGSUSED*/
static	int
logopen(struct dev_open_args *ap)
{
	struct proc *p = curproc;

	KKASSERT(p != NULL);
	if (log_open)
		return (EBUSY);
	log_open = 1;
	callout_init_mp(&logsoftc.sc_callout);
	fsetown(p->p_pid, &logsoftc.sc_sigio);	/* signal process only */
	callout_reset(&logsoftc.sc_callout, hz / log_wakeups_per_second,
		      logtimeout, NULL);
	return (0);
}

/*ARGSUSED*/
static	int
logclose(struct dev_close_args *ap)
{
	log_open = 0;
	callout_stop_sync(&logsoftc.sc_callout);
	logsoftc.sc_state = 0;
	funsetown(&logsoftc.sc_sigio);
	return (0);
}

/*ARGSUSED*/
static	int
logread(struct dev_read_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct msgbuf *mbp = msgbufp;
	int error = 0;
	u_int lindex;
	u_int xindex;
	u_int lindex_modulo;
	u_int n;

	/*
	 * Handle blocking
	 */
	while (mbp->msg_bufl == mbp->msg_bufx) {
		crit_enter();
		if (ap->a_ioflag & IO_NDELAY) {
			crit_exit();
			return (EWOULDBLOCK);
		}
		atomic_set_int(&logsoftc.sc_state, LOG_RDWAIT);
		if ((error = tsleep((caddr_t)mbp, PCATCH, "klog", 0))) {
			crit_exit();
			return (error);
		}
		/* don't bother clearing LOG_RDWAIT */
		crit_exit();
	}

	/*
	 * Loop reading data
	 */
	while (uio->uio_resid > 0 && mbp->msg_bufl != mbp->msg_bufx) {
		lindex = mbp->msg_bufl;
		xindex = mbp->msg_bufx;
		cpu_ccfence();

		/*
		 * Clean up if too much time has passed causing us to wrap
		 * the buffer.  This will lose some data.  If more than ~4GB
		 * then this will lose even more data.
		 */
		n = xindex - lindex;
		if (n > mbp->msg_size - 1024) {
			lindex = xindex - mbp->msg_size + 2048;
			n = xindex - lindex;
		}

		/*
		 * Calculates contiguous bytes we can read in one loop.
		 */
		lindex_modulo = lindex % mbp->msg_size;
		n = mbp->msg_size - lindex_modulo;
		if (n > xindex - lindex)
			n = xindex - lindex;
		if ((size_t)n > uio->uio_resid)
			n = (u_int)uio->uio_resid;

		/*
		 * Copy (n) bytes of data.
		 */
		error = uiomove((caddr_t)msgbufp->msg_ptr + lindex_modulo,
				(size_t)n, uio);
		if (error)
			break;
		mbp->msg_bufl = lindex + n;
	}
	return (error);
}

static struct filterops logread_filtops =
	{ FILTEROP_ISFD, NULL, logfiltdetach, logfiltread };

static int
logkqfilter(struct dev_kqfilter_args *ap)
{
	struct knote *kn = ap->a_kn;
	struct klist *klist = &logsoftc.sc_kqp.ki_note;

	ap->a_result = 0;
	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &logread_filtops;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	knote_insert(klist, kn);

	return (0);
}

static void
logfiltdetach(struct knote *kn)
{
	struct klist *klist = &logsoftc.sc_kqp.ki_note;

	knote_remove(klist, kn);
}

static int
logfiltread(struct knote *kn, long hint)
{
	int ret = 0;

	crit_enter();
	if (msgbufp->msg_bufl != msgbufp->msg_bufx)
		ret = 1;
	crit_exit();

	return (ret);
}

static void
logtimeout(void *arg)
{
	if (!log_open)
		return;
	if (msgbuftrigger == 0) {
		callout_reset(&logsoftc.sc_callout,
			      hz / log_wakeups_per_second, logtimeout, NULL);
		return;
	}
	msgbuftrigger = 0;
	KNOTE(&logsoftc.sc_kqp.ki_note, 0);
	if ((logsoftc.sc_state & LOG_ASYNC) && logsoftc.sc_sigio != NULL)
		pgsigio(logsoftc.sc_sigio, SIGIO, 0);
	if (logsoftc.sc_state & LOG_RDWAIT) {
		atomic_clear_int(&logsoftc.sc_state, LOG_RDWAIT);
		wakeup((caddr_t)msgbufp);
	}
	callout_reset(&logsoftc.sc_callout, hz / log_wakeups_per_second,
		      logtimeout, NULL);
}

/*ARGSUSED*/
static	int
logioctl(struct dev_ioctl_args *ap)
{
	struct msgbuf *mbp = msgbufp;
	u_int lindex;
	u_int xindex;
	u_int n;

	switch (ap->a_cmd) {
	case FIONREAD:
		lindex = mbp->msg_bufl;
		xindex = mbp->msg_bufx;
		cpu_ccfence();

		/*
		 * Clean up if too much time has passed causing us to wrap
		 * the buffer.  This will lose some data.  If more than ~4GB
		 * then this will lose even more data.
		 */
		n = xindex - lindex;
		if (n > mbp->msg_size - 1024) {
			lindex = xindex - mbp->msg_size + 2048;
			n = xindex - lindex;
		}
		*(int *)ap->a_data = n;
		break;

	case FIOASYNC:
		if (*(int *)ap->a_data)
			atomic_set_int(&logsoftc.sc_state, LOG_ASYNC);
		else
			atomic_clear_int(&logsoftc.sc_state, LOG_ASYNC);
		break;

	case FIOSETOWN:
		return (fsetown(*(int *)ap->a_data, &logsoftc.sc_sigio));

	case FIOGETOWN:
		*(int *)ap->a_data = fgetown(&logsoftc.sc_sigio);
		break;

	/* This is deprecated, FIOSETOWN should be used instead. */
	case TIOCSPGRP:
		return (fsetown(-(*(int *)ap->a_data), &logsoftc.sc_sigio));

	/* This is deprecated, FIOGETOWN should be used instead */
	case TIOCGPGRP:
		*(int *)ap->a_data = -fgetown(&logsoftc.sc_sigio);
		break;

	default:
		return (ENOTTY);
	}
	return (0);
}

static void
log_drvinit(void *unused)
{
	make_dev(&log_ops, 0, UID_ROOT, GID_WHEEL, 0600, "klog");
}

SYSINIT(logdev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE + CDEV_MAJOR, log_drvinit,
    NULL);
