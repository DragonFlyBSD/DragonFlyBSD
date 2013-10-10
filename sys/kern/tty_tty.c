/*-
 * (MPSAFE)
 *
 * Copyright (c) 1982, 1986, 1991, 1993
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
 *	@(#)tty_tty.c	8.2 (Berkeley) 9/23/93
 * $FreeBSD: src/sys/kern/tty_tty.c,v 1.30 1999/09/25 18:24:24 phk Exp $
 */

/*
 * Indirect driver for controlling tty.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/lock.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/ttycom.h>
#include <sys/vnode.h>
#include <sys/kernel.h>
#include <sys/poll.h> /* XXX: poll args used in KQ filters */
#include <sys/event.h>

static	d_open_t	cttyopen;
static	d_close_t	cttyclose;
static	d_read_t	cttyread;
static	d_write_t	cttywrite;
static	d_ioctl_t	cttyioctl;
static	d_kqfilter_t	cttykqfilter;

static void cttyfilt_detach(struct knote *);
static int cttyfilt_read(struct knote *, long);
static int cttyfilt_write(struct knote *, long);

#define	CDEV_MAJOR	1
static struct dev_ops ctty_ops = {
	{ "ctty", 0, D_TTY },
	.d_open =	cttyopen,
	.d_close =	cttyclose,
	.d_read =	cttyread,
	.d_write =	cttywrite,
	.d_ioctl =	cttyioctl,
	.d_kqfilter =	cttykqfilter
};

#define cttyvp(p) (((p)->p_flags & P_CONTROLT) ? \
			(p)->p_session->s_ttyvp : NULL)

/*
 * This opens /dev/tty.  Because multiple opens of /dev/tty only
 * generate a single open to the actual tty, the file modes are
 * locked to FREAD|FWRITE.
 */
static	int
cttyopen(struct dev_open_args *ap)
{
	struct proc *p = curproc;
	struct vnode *ttyvp;
	int error;

	KKASSERT(p);
retry:
	if ((ttyvp = cttyvp(p)) == NULL)
		return (ENXIO);
	if (ttyvp->v_flag & VCTTYISOPEN)
		return (0);

	/*
	 * Messy interlock, don't let the vnode go away while we try to
	 * lock it and check for race after we might have blocked.
	 *
	 * WARNING! The device open (devfs_spec_open()) temporarily
	 *	    releases the vnode lock on ttyvp when issuing the
	 *	    dev_dopen(), which means that the VCTTYISOPEn flag
	 *	    can race during the VOP_OPEN().
	 *
	 *	    If something does race we have to undo our potentially
	 *	    extra open.
	 */
	vhold(ttyvp);
	vn_lock(ttyvp, LK_EXCLUSIVE | LK_RETRY);
	if (ttyvp != cttyvp(p) || (ttyvp->v_flag & VCTTYISOPEN)) {
		kprintf("Warning: cttyopen: race-1 avoided\n");
		vn_unlock(ttyvp);
		vdrop(ttyvp);
		goto retry;
	}
	error = VOP_OPEN(ttyvp, FREAD|FWRITE, ap->a_cred, NULL);
	if (ttyvp != cttyvp(p) || (ttyvp->v_flag & VCTTYISOPEN)) {
		kprintf("Warning: cttyopen: race-2 avoided\n");
		if (error == 0)
			VOP_CLOSE(ttyvp, FREAD|FWRITE);
		vn_unlock(ttyvp);
		vdrop(ttyvp);
		goto retry;
	}
	if (error == 0)
		vsetflags(ttyvp, VCTTYISOPEN);
	vn_unlock(ttyvp);
	vdrop(ttyvp);
	return(error);
}

/*
 * This closes /dev/tty.  Because multiple opens of /dev/tty only
 * generate a single open to the actual tty, the file modes are
 * locked to FREAD|FWRITE.
 */
static int
cttyclose(struct dev_close_args *ap)
{
	struct proc *p = curproc;
	struct vnode *ttyvp;
	int error;

	KKASSERT(p);
retry:
	/*
	 * The tty may have been TIOCNOTTY'd, don't return an
	 * error on close.  We just have nothing to do.
	 */
	if ((ttyvp = cttyvp(p)) == NULL)
		return(0);
	if (ttyvp->v_flag & VCTTYISOPEN) {
		/*
		 * Avoid a nasty race if we block while getting the lock.
		 */
		vref(ttyvp);
		error = vn_lock(ttyvp, LK_EXCLUSIVE | LK_RETRY);
		if (error) {
			vrele(ttyvp);
			goto retry;
		}
		if (ttyvp != cttyvp(p) || (ttyvp->v_flag & VCTTYISOPEN) == 0) {
			kprintf("Warning: cttyclose: race avoided\n");
			vn_unlock(ttyvp);
			vrele(ttyvp);
			goto retry;
		}
		vclrflags(ttyvp, VCTTYISOPEN);
		error = VOP_CLOSE(ttyvp, FREAD|FWRITE);
		vn_unlock(ttyvp);
		vrele(ttyvp);
	} else {
		error = 0;
	}
	return(error);
}

/*
 * Read from the controlling terminal (/dev/tty).  The tty is refed as
 * of the cttyvp(), but the ref can get ripped out from under us if
 * the controlling terminal is revoked while we are blocked on the lock,
 * so use vget() instead of vn_lock().
 */
static	int
cttyread(struct dev_read_args *ap)
{
	struct proc *p = curproc;
	struct vnode *ttyvp;
	int error;

	KKASSERT(p);
	ttyvp = cttyvp(p);
	if (ttyvp == NULL)
		return (EIO);
	if ((error = vget(ttyvp, LK_EXCLUSIVE | LK_RETRY)) == 0) {
		error = VOP_READ(ttyvp, ap->a_uio, ap->a_ioflag, NOCRED);
		vput(ttyvp);
	}
	return (error);
}

/*
 * Read from the controlling terminal (/dev/tty).  The tty is refed as
 * of the cttyvp(), but the ref can get ripped out from under us if
 * the controlling terminal is revoked while we are blocked on the lock,
 * so use vget() instead of vn_lock().
 */
static	int
cttywrite(struct dev_write_args *ap)
{
	struct proc *p = curproc;
	struct vnode *ttyvp;
	int error;

	KKASSERT(p);
	ttyvp = cttyvp(p);
	if (ttyvp == NULL)
		return (EIO);
	if ((error = vget(ttyvp, LK_EXCLUSIVE | LK_RETRY)) == 0) {
		error = VOP_WRITE(ttyvp, ap->a_uio, ap->a_ioflag, NOCRED);
		vput(ttyvp);
	}
	return (error);
}

/*ARGSUSED*/
static	int
cttyioctl(struct dev_ioctl_args *ap)
{
	struct vnode *ttyvp;
	struct proc *p = curproc;

	KKASSERT(p);
	lwkt_gettoken(&p->p_token);
	lwkt_gettoken(&proc_token);
	ttyvp = cttyvp(p);
	if (ttyvp == NULL) {
		lwkt_reltoken(&proc_token);
		lwkt_reltoken(&p->p_token);
		return (EIO);
	}
	/*
	 * Don't allow controlling tty to be set to the controlling tty
	 * (infinite recursion).
	 */
	if (ap->a_cmd == TIOCSCTTY) {
		lwkt_reltoken(&proc_token);
		lwkt_reltoken(&p->p_token);
		return EINVAL;
	}
	if (ap->a_cmd == TIOCNOTTY) {
		if (!SESS_LEADER(p)) {
			p->p_flags &= ~P_CONTROLT;
			lwkt_reltoken(&proc_token);
			lwkt_reltoken(&p->p_token);
			return (0);
		} else {
			lwkt_reltoken(&proc_token);
			lwkt_reltoken(&p->p_token);
			return (EINVAL);
		}
	}
	lwkt_reltoken(&proc_token);
	lwkt_reltoken(&p->p_token);

	return (VOP_IOCTL(ttyvp, ap->a_cmd, ap->a_data, ap->a_fflag,
			  ap->a_cred, ap->a_sysmsg));
}

static struct filterops cttyfiltops_read =
	{ FILTEROP_ISFD, NULL, cttyfilt_detach, cttyfilt_read };
static struct filterops cttyfiltops_write =
	{ FILTEROP_ISFD, NULL, cttyfilt_detach, cttyfilt_write };

static int
cttykqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct proc *p = curproc;
	struct knote *kn = ap->a_kn;
	struct vnode *ttyvp;

	KKASSERT(p);
	ttyvp = cttyvp(p);

	if (ttyvp != NULL)
		return (VOP_KQFILTER(ttyvp, kn));

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &cttyfiltops_read;
		kn->kn_hook = (caddr_t)dev;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &cttyfiltops_write;
		kn->kn_hook = (caddr_t)dev;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	return (0);
}

static void
cttyfilt_detach(struct knote *kn) {}

static int
cttyfilt_read(struct knote *kn, long hint)
{
	cdev_t dev = (cdev_t)kn->kn_hook;

	if (seltrue(dev, POLLIN | POLLRDNORM))
		return (1);

	return (0);
}

static int
cttyfilt_write(struct knote *kn, long hint)
{
	cdev_t dev = (cdev_t)kn->kn_hook;

	if (seltrue(dev, POLLOUT | POLLWRNORM))
		return (1);

	return (0);
}

static void
ctty_drvinit(void *unused __unused)
{
	make_dev(&ctty_ops, 0, 0, 0, 0666, "tty");
}

SYSINIT(cttydev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR,ctty_drvinit,NULL)
