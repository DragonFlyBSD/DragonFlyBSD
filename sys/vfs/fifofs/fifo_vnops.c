/*
 * Copyright (c) 1990, 1993, 1995
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
 *	@(#)fifo_vnops.c	8.10 (Berkeley) 5/27/95
 * $FreeBSD: src/sys/miscfs/fifofs/fifo_vnops.c,v 1.45.2.4 2003/04/22 10:11:24 bde Exp $
 * $DragonFly: src/sys/vfs/fifofs/fifo_vnops.c,v 1.34 2006/09/05 00:55:50 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/unistd.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/thread2.h>
#include <sys/vnode.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/event.h>
#include <sys/poll.h>
#include <sys/un.h>

#include <sys/thread2.h>

#include "fifo.h"

/*
 * This structure is associated with the FIFO vnode and stores
 * the state associated with the FIFO.
 */
struct fifoinfo {
	struct socket	*fi_readsock;
	struct socket	*fi_writesock;
	long		fi_readers;
	long		fi_writers;
};

static int	fifo_badop (void);
static int	fifo_print (struct vop_print_args *);
static int	fifo_lookup (struct vop_old_lookup_args *);
static int	fifo_open (struct vop_open_args *);
static int	fifo_close (struct vop_close_args *);
static int	fifo_read (struct vop_read_args *);
static int	fifo_write (struct vop_write_args *);
static int	fifo_ioctl (struct vop_ioctl_args *);
static int	fifo_poll (struct vop_poll_args *);
static int	fifo_kqfilter (struct vop_kqfilter_args *);
static int	fifo_inactive (struct  vop_inactive_args *);
static int	fifo_bmap (struct vop_bmap_args *);
static int	fifo_pathconf (struct vop_pathconf_args *);
static int	fifo_advlock (struct vop_advlock_args *);

static void	filt_fifordetach(struct knote *kn);
static int	filt_fiforead(struct knote *kn, long hint);
static void	filt_fifowdetach(struct knote *kn);
static int	filt_fifowrite(struct knote *kn, long hint);

static struct filterops fiforead_filtops =
	{ 1, NULL, filt_fifordetach, filt_fiforead };
static struct filterops fifowrite_filtops =
	{ 1, NULL, filt_fifowdetach, filt_fifowrite };
  
struct vop_ops fifo_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		(void *)vop_ebadf,
	.vop_advlock =		fifo_advlock,
	.vop_bmap =		fifo_bmap,
	.vop_close =		fifo_close,
	.vop_old_create =	(void *)fifo_badop,
	.vop_getattr =		(void *)vop_ebadf,
	.vop_inactive =		fifo_inactive,
	.vop_ioctl =		fifo_ioctl,
	.vop_kqfilter =		fifo_kqfilter,
	.vop_old_link =		(void *)fifo_badop,
	.vop_old_lookup =	fifo_lookup,
	.vop_old_mkdir =	(void *)fifo_badop,
	.vop_old_mknod =	(void *)fifo_badop,
	.vop_open =		fifo_open,
	.vop_pathconf =		fifo_pathconf,
	.vop_poll =		fifo_poll,
	.vop_print =		fifo_print,
	.vop_read =		fifo_read,
	.vop_readdir =		(void *)fifo_badop,
	.vop_readlink =		(void *)fifo_badop,
	.vop_reallocblks =	(void *)fifo_badop,
	.vop_reclaim =		(void *)vop_null,
	.vop_old_remove =	(void *)fifo_badop,
	.vop_old_rename =	(void *)fifo_badop,
	.vop_old_rmdir =	(void *)fifo_badop,
	.vop_setattr =		(void *)vop_ebadf,
	.vop_old_symlink =	(void *)fifo_badop,
	.vop_write =		fifo_write
};

VNODEOP_SET(fifo_vnode_vops);

static MALLOC_DEFINE(M_FIFOINFO, "Fifo info", "Fifo info entries");

/*
 * fifo_vnoperate()
 */
int
fifo_vnoperate(struct vop_generic_args *ap)
{
	return (VOCALL(&fifo_vnode_vops, ap));
}

/*
 * Trivial lookup routine that always fails.
 *
 * fifo_lookup(struct vnode *a_dvp, struct vnode **a_vpp,
 *	       struct componentname *a_cnp)
 */
/* ARGSUSED */
static int
fifo_lookup(struct vop_old_lookup_args *ap)
{
	*ap->a_vpp = NULL;
	return (ENOTDIR);
}

/*
 * Open called to set up a new instance of a fifo or
 * to find an active instance of a fifo.
 *
 * fifo_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	     struct thread *a_td)
 */
/* ARGSUSED */
static int
fifo_open(struct vop_open_args *ap)
{
	struct thread *td = curthread;
	struct vnode *vp = ap->a_vp;
	struct fifoinfo *fip;
	struct socket *rso, *wso;
	int error;

	if ((fip = vp->v_fifoinfo) == NULL) {
		MALLOC(fip, struct fifoinfo *, sizeof(*fip), M_FIFOINFO, M_WAITOK);
		vp->v_fifoinfo = fip;
		error = socreate(AF_LOCAL, &rso, SOCK_STREAM, 0, td);
		if (error) {
			kfree(fip, M_FIFOINFO);
			vp->v_fifoinfo = NULL;
			return (error);
		}
		fip->fi_readsock = rso;
		error = socreate(AF_LOCAL, &wso, SOCK_STREAM, 0, td);
		if (error) {
			soclose(rso, FNONBLOCK);
			kfree(fip, M_FIFOINFO);
			vp->v_fifoinfo = NULL;
			return (error);
		}
		fip->fi_writesock = wso;
		error = unp_connect2(wso, rso);
		if (error) {
			soclose(wso, FNONBLOCK);
			soclose(rso, FNONBLOCK);
			kfree(fip, M_FIFOINFO);
			vp->v_fifoinfo = NULL;
			return (error);
		}
		fip->fi_readers = fip->fi_writers = 0;
		wso->so_snd.sb_lowat = PIPE_BUF;
		rso->so_state |= SS_CANTRCVMORE;
	}
	if (ap->a_mode & FREAD) {
		fip->fi_readers++;
		if (fip->fi_readers == 1) {
			fip->fi_writesock->so_state &= ~SS_CANTSENDMORE;
			if (fip->fi_writers > 0) {
				wakeup((caddr_t)&fip->fi_writers);
				sowwakeup(fip->fi_writesock);
			}
		}
	}
	if (ap->a_mode & FWRITE) {
		fip->fi_writers++;
		if (fip->fi_writers == 1) {
			fip->fi_readsock->so_state &= ~SS_CANTRCVMORE;
			if (fip->fi_readers > 0) {
				wakeup((caddr_t)&fip->fi_readers);
				sorwakeup(fip->fi_writesock);
			}
		}
	}
	if ((ap->a_mode & FREAD) && (ap->a_mode & O_NONBLOCK) == 0) {
		if (fip->fi_writers == 0) {
			vn_unlock(vp);
			error = tsleep((caddr_t)&fip->fi_readers,
			    PCATCH, "fifoor", 0);
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
			if (error)
				goto bad;
			/*
			 * We must have got woken up because we had a writer.
			 * That (and not still having one) is the condition
			 * that we must wait for.
			 */
		}
	}
	if (ap->a_mode & FWRITE) {
		if (ap->a_mode & O_NONBLOCK) {
			if (fip->fi_readers == 0) {
				error = ENXIO;
				goto bad;
			}
		} else {
			if (fip->fi_readers == 0) {
				vn_unlock(vp);
				error = tsleep((caddr_t)&fip->fi_writers,
				    PCATCH, "fifoow", 0);
				vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
				if (error)
					goto bad;
				/*
				 * We must have got woken up because we had
				 * a reader.  That (and not still having one)
				 * is the condition that we must wait for.
				 */
			}
		}
	}
	return (vop_stdopen(ap));
bad:
	vop_stdopen(ap);	/* bump opencount/writecount as appropriate */
	VOP_CLOSE(vp, ap->a_mode);
	return (error);
}

/*
 * Vnode op for read
 *
 * fifo_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	     struct ucred *a_cred)
 */
/* ARGSUSED */
static int
fifo_read(struct vop_read_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct socket *rso = ap->a_vp->v_fifoinfo->fi_readsock;
	int error, startresid;
	int flags;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_READ)
		panic("fifo_read mode");
#endif
	if (uio->uio_resid == 0)
		return (0);
	if (ap->a_ioflag & IO_NDELAY)
		flags = MSG_FNONBLOCKING;
	else
		flags = 0;
	startresid = uio->uio_resid;
	vn_unlock(ap->a_vp);
	error = soreceive(rso, (struct sockaddr **)0, uio, (struct mbuf **)0,
	    (struct mbuf **)0, &flags);
	vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY);
	return (error);
}

/*
 * Vnode op for write
 *
 * fifo_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	      struct ucred *a_cred)
 */
/* ARGSUSED */
static int
fifo_write(struct vop_write_args *ap)
{
	struct socket *wso = ap->a_vp->v_fifoinfo->fi_writesock;
	struct thread *td = ap->a_uio->uio_td;
	int error;
	int flags;

#ifdef DIAGNOSTIC
	if (ap->a_uio->uio_rw != UIO_WRITE)
		panic("fifo_write mode");
#endif
	if (ap->a_ioflag & IO_NDELAY)
		flags = MSG_FNONBLOCKING;
	else
		flags = 0;
	vn_unlock(ap->a_vp);
	error = sosend(wso, (struct sockaddr *)0, ap->a_uio, 0,
		       (struct mbuf *)0, flags, td);
	vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY);
	return (error);
}

/*
 * Device ioctl operation.
 *
 * fifo_ioctl(struct vnode *a_vp, int a_command, caddr_t a_data, int a_fflag,
 *	      struct ucred *a_cred, struct thread *a_td)
 */
/* ARGSUSED */
static int
fifo_ioctl(struct vop_ioctl_args *ap)
{
	struct file filetmp;	/* Local */
	int error;

	if (ap->a_fflag & FREAD) {
		filetmp.f_data = ap->a_vp->v_fifoinfo->fi_readsock;
		error = soo_ioctl(&filetmp, ap->a_command, ap->a_data, ap->a_cred);
		if (error)
			return (error);
	}
	if (ap->a_fflag & FWRITE) {
		filetmp.f_data = ap->a_vp->v_fifoinfo->fi_writesock;
		error = soo_ioctl(&filetmp, ap->a_command, ap->a_data, ap->a_cred);
		if (error)
			return (error);
	}
	return (0);
}

/*
 * fifo_kqfilter(struct vnode *a_vp, struct knote *a_kn)
 */
/* ARGSUSED */
static int
fifo_kqfilter(struct vop_kqfilter_args *ap)
{
	struct fifoinfo *fi = ap->a_vp->v_fifoinfo;
	struct socket *so;
	struct sockbuf *sb;

	switch (ap->a_kn->kn_filter) {
	case EVFILT_READ:
		ap->a_kn->kn_fop = &fiforead_filtops;
		so = fi->fi_readsock;
		sb = &so->so_rcv;
		break;
	case EVFILT_WRITE:
		ap->a_kn->kn_fop = &fifowrite_filtops;
		so = fi->fi_writesock;
		sb = &so->so_snd;
		break;
	default:
		return (1);
	}

	ap->a_kn->kn_hook = (caddr_t)so;

	SLIST_INSERT_HEAD(&sb->sb_sel.si_note, ap->a_kn, kn_selnext);
	sb->sb_flags |= SB_KNOTE;

	return (0);
}

static void
filt_fifordetach(struct knote *kn)
{
	struct socket *so = (struct socket *)kn->kn_hook;

	SLIST_REMOVE(&so->so_rcv.sb_sel.si_note, kn, knote, kn_selnext);
	if (SLIST_EMPTY(&so->so_rcv.sb_sel.si_note))
		so->so_rcv.sb_flags &= ~SB_KNOTE;
}

static int
filt_fiforead(struct knote *kn, long hint)
{
	struct socket *so = (struct socket *)kn->kn_hook;

	kn->kn_data = so->so_rcv.sb_cc;
	if (so->so_state & SS_CANTRCVMORE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}
	kn->kn_flags &= ~EV_EOF;
	return (kn->kn_data > 0);
}

static void
filt_fifowdetach(struct knote *kn)
{
	struct socket *so = (struct socket *)kn->kn_hook;

	SLIST_REMOVE(&so->so_snd.sb_sel.si_note, kn, knote, kn_selnext);
	if (SLIST_EMPTY(&so->so_snd.sb_sel.si_note))
		so->so_snd.sb_flags &= ~SB_KNOTE;
}

static int
filt_fifowrite(struct knote *kn, long hint)
{
	struct socket *so = (struct socket *)kn->kn_hook;

	kn->kn_data = sbspace(&so->so_snd);
	if (so->so_state & SS_CANTSENDMORE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}
	kn->kn_flags &= ~EV_EOF;
	return (kn->kn_data >= so->so_snd.sb_lowat);
}

/*
 * fifo_poll(struct vnode *a_vp, int a_events, struct ucred *a_cred,
 *	     struct thread *a_td)
 */
/* ARGSUSED */
static int
fifo_poll(struct vop_poll_args *ap)
{
	struct file filetmp;
	int events, revents = 0;

	events = ap->a_events &
		(POLLIN | POLLINIGNEOF | POLLPRI | POLLRDNORM | POLLRDBAND);
	if (events) {
		/*
		 * If POLLIN or POLLRDNORM is requested and POLLINIGNEOF is
		 * not, then convert the first two to the last one.  This
		 * tells the socket poll function to ignore EOF so that we
		 * block if there is no writer (and no data).  Callers can
		 * set POLLINIGNEOF to get non-blocking behavior.
		 */
		if (events & (POLLIN | POLLRDNORM) &&
			!(events & POLLINIGNEOF)) {
			events &= ~(POLLIN | POLLRDNORM);
			events |= POLLINIGNEOF;
		}
		
		filetmp.f_data = ap->a_vp->v_fifoinfo->fi_readsock;
		if (filetmp.f_data)
			revents |= soo_poll(&filetmp, events, ap->a_cred);

		/* Reverse the above conversion. */
		if ((revents & POLLINIGNEOF) &&
			!(ap->a_events & POLLINIGNEOF)) {
			revents |= (ap->a_events & (POLLIN | POLLRDNORM));
			revents &= ~POLLINIGNEOF;
		}
	}
	events = ap->a_events & (POLLOUT | POLLWRNORM | POLLWRBAND);
	if (events) {
		filetmp.f_data = ap->a_vp->v_fifoinfo->fi_writesock;
		if (filetmp.f_data)
			revents |= soo_poll(&filetmp, events, ap->a_cred);
	}
	return (revents);
}

/*
 * fifo_inactive(struct vnode *a_vp, struct thread *a_td)
 */
static int
fifo_inactive(struct vop_inactive_args *ap)
{
	return (0);
}

/*
 * This is a noop, simply returning what one has been given.
 *
 * fifo_bmap(struct vnode *a_vp, off_t a_loffset, struct vnode **a_vpp,
 *	     off_t *a_doffsetp, int *a_runp, int *a_runb)
 */
static int
fifo_bmap(struct vop_bmap_args *ap)
{
	if (ap->a_vpp != NULL)
		*ap->a_vpp = ap->a_vp;
	if (ap->a_doffsetp != NULL)
		*ap->a_doffsetp = ap->a_loffset;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;
	return (0);
}

/*
 * Device close routine
 *
 * fifo_close(struct vnode *a_vp, int a_fflag, struct ucred *a_cred,
 *	      struct thread *a_td)
 */
/* ARGSUSED */
static int
fifo_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fifoinfo *fip = vp->v_fifoinfo;
	int error1, error2;

	if (ap->a_fflag & FREAD) {
		fip->fi_readers--;
		if (fip->fi_readers == 0)
			socantsendmore(fip->fi_writesock);
	}
	if (ap->a_fflag & FWRITE) {
		fip->fi_writers--;
		if (fip->fi_writers == 0)
			socantrcvmore(fip->fi_readsock);
	}
	if (vp->v_usecount > 1) {
		vop_stdclose(ap);
		return (0);
	}
	error1 = soclose(fip->fi_readsock, FNONBLOCK);
	error2 = soclose(fip->fi_writesock, FNONBLOCK);
	FREE(fip, M_FIFOINFO);
	vp->v_fifoinfo = NULL;
	if (error1)
		return (error1);
	vop_stdclose(ap);
	return (error2);
}


/*
 * Print out internal contents of a fifo vnode.
 */
int
fifo_printinfo(struct vnode *vp)
{
	struct fifoinfo *fip = vp->v_fifoinfo;

	printf(", fifo with %ld readers and %ld writers",
		fip->fi_readers, fip->fi_writers);
	return (0);
}

/*
 * Print out the contents of a fifo vnode.
 *
 * fifo_print(struct vnode *a_vp)
 */
static int
fifo_print(struct vop_print_args *ap)
{
	printf("tag VT_NON");
	fifo_printinfo(ap->a_vp);
	printf("\n");
	return (0);
}

/*
 * Return POSIX pathconf information applicable to fifo's.
 *
 * fifo_pathconf(struct vnode *a_vp, int a_name, int *a_retval)
 */
int
fifo_pathconf(struct vop_pathconf_args *ap)
{
	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		return (0);
	case _PC_PIPE_BUF:
		*ap->a_retval = PIPE_BUF;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}

/*
 * Fifo advisory byte-level locks.
 *
 * fifo_advlock(struct vnode *a_vp, caddr_t a_id, int a_op, struct flock *a_fl,
 *		int a_flags)
 */
/* ARGSUSED */
static int
fifo_advlock(struct vop_advlock_args *ap)
{
	return ((ap->a_flags & F_POSIX) ? EINVAL : EOPNOTSUPP);
}

/*
 * Fifo bad operation
 */
static int
fifo_badop(void)
{
	panic("fifo_badop called");
	/* NOTREACHED */
}
