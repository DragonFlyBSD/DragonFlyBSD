/*-
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
 *	@(#)tty_tty.c	8.2 (Berkeley) 9/23/93
 * $FreeBSD: src/sys/kern/tty_tty.c,v 1.30 1999/09/25 18:24:24 phk Exp $
 * $DragonFly: src/sys/kern/tty_tty.c,v 1.10 2004/10/12 19:20:46 dillon Exp $
 */

/*
 * Indirect driver for controlling tty.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/ttycom.h>
#include <sys/vnode.h>
#include <sys/kernel.h>

static	d_open_t	cttyopen;
static	d_close_t	cttyclose;
static	d_read_t	cttyread;
static	d_write_t	cttywrite;
static	d_ioctl_t	cttyioctl;
static	d_poll_t	cttypoll;

#define	CDEV_MAJOR	1
/* Don't make this static, since fdesc_vnops uses it. */
struct cdevsw ctty_cdevsw = {
	/* name */	"ctty",
	/* maj */	CDEV_MAJOR,
	/* flags */	D_TTY,
	/* port */	NULL,
	/* clone */	NULL,

	/* open */	cttyopen,
	/* close */	cttyclose,
	/* read */	cttyread,
	/* write */	cttywrite,
	/* ioctl */	cttyioctl,
	/* poll */	cttypoll,
	/* mmap */	nommap,
	/* strategy */	nostrategy,
	/* dump */	nodump,
	/* psize */	nopsize
};

#define cttyvp(p) ((p)->p_flag & P_CONTROLT ? (p)->p_session->s_ttyvp : NULL)

/*ARGSUSED*/
static	int
cttyopen(dev_t dev, int flag, int mode, struct thread *td)
{
	struct proc *p = td->td_proc;
	struct vnode *ttyvp;
	int error;

	KKASSERT(p);
	ttyvp = cttyvp(p);
	if (ttyvp) {
		if (ttyvp->v_flag & VCTTYISOPEN) {
			error = 0;
		} else {
			vsetflags(ttyvp, VCTTYISOPEN);
			vn_lock(ttyvp, LK_EXCLUSIVE | LK_RETRY, td);
			error = VOP_OPEN(ttyvp, flag, NOCRED, td);
			VOP_UNLOCK(ttyvp, 0, td);
		}
	} else {
		error = ENXIO;
	}
	return (error);
}

static int
cttyclose(dev_t dev, int fflag, int devtype, struct thread *td)
{
	struct proc *p = td->td_proc;
	struct vnode *ttyvp;
	int error;

	KKASSERT(p);
	ttyvp = cttyvp(p);
	if (ttyvp == NULL) {
		/*
		 * The tty may have been TIOCNOTTY'd, don't return an
		 * error on close.  We just have nothing to do.
		 */
		/* error = EIO; */
		error = 0;
	} else if (ttyvp->v_flag & VCTTYISOPEN) {
		vclrflags(ttyvp, VCTTYISOPEN);
		error = vn_lock(ttyvp, LK_EXCLUSIVE | LK_RETRY, td);
		if (error == 0) {
			error = VOP_CLOSE(ttyvp, fflag, td);
			VOP_UNLOCK(ttyvp, 0, td);
		}
	} else {
		error = 0;
	}
	return(error);
}

/*ARGSUSED*/
static	int
cttyread(dev, uio, flag)
	dev_t dev;
	struct uio *uio;
	int flag;
{
	struct thread *td = uio->uio_td;
	struct proc *p = td->td_proc;
	struct vnode *ttyvp;
	int error;

	KKASSERT(p);
	ttyvp = cttyvp(p);
	if (ttyvp == NULL)
		return (EIO);
	vn_lock(ttyvp, LK_EXCLUSIVE | LK_RETRY, td);
	error = VOP_READ(ttyvp, uio, flag, NOCRED);
	VOP_UNLOCK(ttyvp, 0, td);
	return (error);
}

/*ARGSUSED*/
static	int
cttywrite(dev, uio, flag)
	dev_t dev;
	struct uio *uio;
	int flag;
{
	struct thread *td = uio->uio_td;
	struct proc *p = td->td_proc;
	struct vnode *ttyvp;
	int error;

	KKASSERT(p);
	ttyvp = cttyvp(p);
	if (ttyvp == NULL)
		return (EIO);
	vn_lock(ttyvp, LK_EXCLUSIVE | LK_RETRY, td);
	error = VOP_WRITE(ttyvp, uio, flag, NOCRED);
	VOP_UNLOCK(ttyvp, 0, td);
	return (error);
}

/*ARGSUSED*/
static	int
cttyioctl(dev, cmd, addr, flag, td)
	dev_t dev;
	u_long cmd;
	caddr_t addr;
	int flag;
	struct thread *td;
{
	struct vnode *ttyvp;
	struct proc *p = td->td_proc;

	KKASSERT(p);
	ttyvp = cttyvp(p);
	if (ttyvp == NULL)
		return (EIO);
	if (cmd == TIOCSCTTY)  /* don't allow controlling tty to be set    */
		return EINVAL; /* to controlling tty -- infinite recursion */
	if (cmd == TIOCNOTTY) {
		if (!SESS_LEADER(p)) {
			p->p_flag &= ~P_CONTROLT;
			return (0);
		} else
			return (EINVAL);
	}
	return (VOP_IOCTL(ttyvp, cmd, addr, flag, NOCRED, td));
}

/*ARGSUSED*/
static	int
cttypoll(dev_t dev, int events, struct thread *td)
{
	struct vnode *ttyvp;
	struct proc *p = td->td_proc;

	KKASSERT(p);
	ttyvp = cttyvp(p);
	if (ttyvp == NULL)
		/* try operation to get EOF/failure */
		return (seltrue(dev, events, td));
	return (VOP_POLL(ttyvp, events, p->p_ucred, td));
}

static void ctty_drvinit (void *unused);
static void
ctty_drvinit(unused)
	void *unused;
{
	cdevsw_add(&ctty_cdevsw, 0, 0);
	make_dev(&ctty_cdevsw, 0, 0, 0, 0666, "tty");
}

SYSINIT(cttydev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR,ctty_drvinit,NULL)
