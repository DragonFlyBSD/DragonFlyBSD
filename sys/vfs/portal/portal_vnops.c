/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)portal_vnops.c	8.14 (Berkeley) 5/21/95
 *
 * $FreeBSD: src/sys/miscfs/portal/portal_vnops.c,v 1.38 1999/12/21 06:29:00 chris Exp $
 */

/*
 * Portal Filesystem
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/namei.h>
#include <sys/mbuf.h>
#include <sys/resourcevar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/un.h>
#include <sys/unpcb.h>
#include "portal.h"

#include <sys/thread2.h>

static int portal_fileid = PORTAL_ROOTFILEID+1;

static void	portal_closefd (struct thread *td, int fd);
static int	portal_connect (struct socket *so, struct socket *so2);
static int	portal_getattr (struct vop_getattr_args *ap);
static int	portal_inactive (struct vop_inactive_args *ap);
static int	portal_lookup (struct vop_old_lookup_args *ap);
static int	portal_open (struct vop_open_args *ap);
static int	portal_print (struct vop_print_args *ap);
static int	portal_readdir (struct vop_readdir_args *ap);
static int	portal_reclaim (struct vop_reclaim_args *ap);
static int	portal_setattr (struct vop_setattr_args *ap);

static void
portal_closefd(struct thread *td, int fd)
{
	int error;
	struct close_args ua;

	ua.fd = fd;
	error = sys_close(&ua);
	/*
	 * We should never get an error, and there isn't anything
	 * we could do if we got one, so just print a message.
	 */
	if (error)
		kprintf("portal_closefd: error = %d\n", error);
}

/*
 * vp is the current namei directory
 * cnp is the name to locate in that directory...
 *
 * portal_lookup(struct vnode *a_dvp, struct vnode **a_vpp,
 *		 struct componentname *a_cnp)
 */
static int
portal_lookup(struct vop_old_lookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct vnode **vpp = ap->a_vpp;
	struct vnode *dvp = ap->a_dvp;
	char *pname = cnp->cn_nameptr;
	struct portalnode *pt;
	int error;
	struct vnode *fvp = NULL;
	char *path;
	int size;

	*vpp = NULLVP;

	if (cnp->cn_nameiop == NAMEI_DELETE || cnp->cn_nameiop == NAMEI_RENAME)
		return (EROFS);

	if (cnp->cn_namelen == 1 && *pname == '.') {
		*vpp = dvp;
		vref(dvp);
		return (0);
	}

	/*
	 * Do the MALLOC before the getnewvnode since doing so afterward
	 * might cause a bogus v_data pointer to get dereferenced
	 * elsewhere if MALLOC should block.
	 */
	pt = kmalloc(sizeof(struct portalnode), M_TEMP, M_WAITOK);

	error = getnewvnode(VT_PORTAL, dvp->v_mount, &fvp, 0, 0);
	if (error) {
		kfree(pt, M_TEMP);
		goto bad;
	}
	fvp->v_type = VREG;
	fvp->v_data = pt;

	/*
	 * Save all of the remaining pathname and
	 * advance the namei next pointer to the end
	 * of the string.
	 */
	for (size = 0, path = pname; *path; path++)
		size++;
	cnp->cn_consume = size - cnp->cn_namelen;

	pt->pt_arg = kmalloc(size+1, M_TEMP, M_WAITOK);
	pt->pt_size = size+1;
	bcopy(pname, pt->pt_arg, pt->pt_size);
	pt->pt_fileid = portal_fileid++;

	*vpp = fvp;
	vx_unlock(fvp);
	return (0);

bad:;
	if (fvp)
		vrele(fvp);
	return (error);
}

static int
portal_connect(struct socket *so, struct socket *so2)
{
	/* from unp_connect, bypassing the namei stuff... */
	struct socket *so3;
	struct unpcb *unp2;
	struct unpcb *unp3;

	if (so2 == NULL)
		return (ECONNREFUSED);

	if (so->so_type != so2->so_type)
		return (EPROTOTYPE);

	if ((so2->so_options & SO_ACCEPTCONN) == 0)
		return (ECONNREFUSED);

	if ((so3 = sonewconn(so2, 0)) == NULL)
		return (ECONNREFUSED);

	unp2 = so2->so_pcb;
	unp3 = so3->so_pcb;
	if (unp2->unp_addr)
		unp3->unp_addr = (struct sockaddr_un *)
			dup_sockaddr((struct sockaddr *)unp2->unp_addr);
	so2 = so3;

	return (unp_connect2(so, so2));
}

/*
 * portal_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	       struct file *a_fp)
 */
static int
portal_open(struct vop_open_args *ap)
{
	struct socket *so = NULL;
	struct portalnode *pt;
	struct thread *td = curthread;
	struct vnode *vp = ap->a_vp;
	struct uio auio;
	struct iovec aiov[2];
	struct sockbuf sio;
	int res;
	struct mbuf *cm = NULL;
	struct cmsghdr *cmsg;
	int newfds;
	int *ip;
	int fd;
	int error;
	int len;
	struct portalmount *fmp;
	struct file *fp;
	struct portal_cred pcred;

	/*
	 * Nothing to do when opening the root node.
	 */
	if (vp->v_flag & VROOT)
		return (vop_stdopen(ap));

	/*
	 * Can't be opened unless the caller is set up
	 * to deal with the side effects.  Check for this
	 * by testing whether the p_dupfd has been set.
	 */
	KKASSERT(td->td_proc);
	if (td->td_lwp->lwp_dupfd >= 0)
		return (ENODEV);

	pt = VTOPORTAL(vp);
	fmp = VFSTOPORTAL(vp->v_mount);

	/*
	 * Create a new socket.
	 */
	error = socreate(AF_UNIX, &so, SOCK_STREAM, 0, td);
	if (error)
		goto bad;

	/*
	 * Reserve some buffer space
	 */
	res = pt->pt_size + sizeof(pcred) + 512;	/* XXX */
	error = soreserve(so, res, res, &td->td_proc->p_rlimit[RLIMIT_SBSIZE]);
	if (error)
		goto bad;

	/*
	 * Kick off connection
	 */
	error = portal_connect(so, (struct socket *)fmp->pm_server->f_data);
	if (error)
		goto bad;

	/*
	 * Wait for connection to complete
	 */
	/*
	 * XXX: Since the mount point is holding a reference on the
	 * underlying server socket, it is not easy to find out whether
	 * the server process is still running.  To handle this problem
	 * we loop waiting for the new socket to be connected (something
	 * which will only happen if the server is still running) or for
	 * the reference count on the server socket to drop to 1, which
	 * will happen if the server dies.  Sleep for 5 second intervals
	 * and keep polling the reference count.   XXX.
	 */
	crit_enter();
	while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
		if (fmp->pm_server->f_count == 1) {
			error = ECONNREFUSED;
			crit_exit();
			goto bad;
		}
		(void) tsleep((caddr_t) &so->so_timeo, 0, "portalcon", 5 * hz);
	}
	crit_exit();

	if (so->so_error) {
		error = so->so_error;
		goto bad;
	}

	/*
	 * Set miscellaneous flags
	 */
	so->so_rcv.ssb_timeo = 0;
	so->so_snd.ssb_timeo = 0;
	atomic_set_int(&so->so_rcv.ssb_flags, SSB_NOINTR);
	atomic_set_int(&so->so_snd.ssb_flags, SSB_NOINTR);


	pcred.pcr_flag = ap->a_mode;
	pcred.pcr_uid = ap->a_cred->cr_uid;
	pcred.pcr_ngroups = ap->a_cred->cr_ngroups;
	bcopy(ap->a_cred->cr_groups, pcred.pcr_groups, NGROUPS * sizeof(gid_t));
	aiov[0].iov_base = (caddr_t) &pcred;
	aiov[0].iov_len = sizeof(pcred);
	aiov[1].iov_base = pt->pt_arg;
	aiov[1].iov_len = pt->pt_size;
	auio.uio_iov = aiov;
	auio.uio_iovcnt = 2;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = td;
	auio.uio_offset = 0;
	auio.uio_resid = aiov[0].iov_len + aiov[1].iov_len;

	error = sosend(so, NULL, &auio, NULL, NULL, 0, td);
	if (error)
		goto bad;

	len = sizeof(int);
	sbinit(&sio, len);
	do {
		struct mbuf *m;
		int flags;

		flags = MSG_WAITALL;
		error = soreceive(so, NULL, NULL, &sio, &cm, &flags);
		if (error)
			goto bad;

		/*
		 * Grab an error code from the mbuf.
		 */
		if ((m = sio.sb_mb) != NULL) {
			m = m_pullup(m, sizeof(int));	/* Needed? */
			if (m) {
				error = *(mtod(m, int *));
				m_freem(m);
			} else {
				error = EINVAL;
			}
		} else {
			if (cm == NULL) {
				error = ECONNRESET;	 /* XXX */
#ifdef notdef
				break;
#endif
			}
		}
	} while (cm == NULL && sio.sb_cc == 0 && !error);

	if (cm == NULL)
		goto bad;

	if (auio.uio_resid) {
		error = 0;
#ifdef notdef
		error = EMSGSIZE;
		goto bad;
#endif
	}

	/*
	 * XXX: Break apart the control message, and retrieve the
	 * received file descriptor.  Note that more than one descriptor
	 * may have been received, or that the rights chain may have more
	 * than a single mbuf in it.  What to do?
	 */
	cmsg = mtod(cm, struct cmsghdr *);
	newfds = (cmsg->cmsg_len - sizeof(*cmsg)) / sizeof (int);
	if (newfds == 0) {
		error = ECONNREFUSED;
		goto bad;
	}
	/*
	 * At this point the rights message consists of a control message
	 * header, followed by a data region containing a vector of
	 * integer file descriptors.  The fds were allocated by the action
	 * of receiving the control message.
	 */
	ip = (int *) (cmsg + 1);
	fd = *ip++;
	if (newfds > 1) {
		/*
		 * Close extra fds.
		 */
		int i;
		kprintf("portal_open: %d extra fds\n", newfds - 1);
		for (i = 1; i < newfds; i++) {
			portal_closefd(td, *ip);
			ip++;
		}
	}

	/*
	 * Check that the mode the file is being opened for is a subset
	 * of the mode of the existing descriptor.
	 */
	KKASSERT(td->td_proc);
 	fp = td->td_proc->p_fd->fd_files[fd].fp;
	if (((ap->a_mode & (FREAD|FWRITE)) | fp->f_flag) != fp->f_flag) {
		portal_closefd(td, fd);
		error = EACCES;
		goto bad;
	}

	/*
	 * Save the dup fd in the proc structure then return the
	 * special error code (ENXIO) which causes magic things to
	 * happen in vn_open.  The whole concept is, well, hmmm.
	 */
	td->td_lwp->lwp_dupfd = fd;
	vop_stdopen(ap);
	error = ENXIO;

bad:;
	/*
	 * And discard the control message.
	 */
	if (cm) {
		m_freem(cm);
	}

	if (so) {
		soshutdown(so, SHUT_RDWR);
		soclose(so, FNONBLOCK);
	}
	return (error);
}

/*
 * portal_getattr(struct vnode *a_vp, struct vattr *a_vap)
 */
static int
portal_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;

	bzero(vap, sizeof(*vap));
	vattr_null(vap);
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_size = DEV_BSIZE;
	vap->va_blocksize = DEV_BSIZE;
	nanotime(&vap->va_atime);
	vap->va_mtime = vap->va_atime;
	vap->va_ctime = vap->va_mtime;
	vap->va_gen = 0;
	vap->va_flags = 0;
	vap->va_rdev = makedev(VNOVAL, VNOVAL);
	/* vap->va_qbytes = 0; */
	vap->va_bytes = 0;
	/* vap->va_qsize = 0; */
	if (vp->v_flag & VROOT) {
		vap->va_type = VDIR;
		vap->va_mode = S_IRUSR|S_IWUSR|S_IXUSR|
				S_IRGRP|S_IWGRP|S_IXGRP|
				S_IROTH|S_IWOTH|S_IXOTH;
		vap->va_nlink = 2;
		vap->va_fileid = 2;
	} else {
		vap->va_type = VREG;
		vap->va_mode = S_IRUSR|S_IWUSR|
				S_IRGRP|S_IWGRP|
				S_IROTH|S_IWOTH;
		vap->va_nlink = 1;
		vap->va_fileid = VTOPORTAL(vp)->pt_fileid;
	}
	return (0);
}

/*
 * portal_setattr(struct vnode *a_vp, struct vattr *a_vap,
 *		  struct ucred *a_cred)
 */
static int
portal_setattr(struct vop_setattr_args *ap)
{
	/*
	 * Can't mess with the root vnode
	 */
	if (ap->a_vp->v_flag & VROOT)
		return (EACCES);

	if (ap->a_vap->va_flags != VNOVAL)
		return (EOPNOTSUPP);

	return (0);
}

/*
 * Fake readdir, just return empty directory.
 * It is hard to deal with '.' and '..' so don't bother.
 *
 * portal_readdir(struct vnode *a_vp, struct uio *a_uio,
 *		  struct ucred *a_cred, int *a_eofflag,
 *		  off_t *a_cookies, int a_ncookies)
 */
static int
portal_readdir(struct vop_readdir_args *ap)
{
	/*
	 * We don't allow exporting portal mounts, and currently local
	 * requests do not need cookies.
	 */
	if (ap->a_ncookies)
		panic("portal_readdir: not hungry");

	return (0);
}

/*
 * portal_inactive(struct vnode *a_vp)
 */
static int
portal_inactive(struct vop_inactive_args *ap)
{
	return (0);
}

/*
 * portal_reclaim(struct vnode *a_vp)
 */
static int
portal_reclaim(struct vop_reclaim_args *ap)
{
	struct portalnode *pt = VTOPORTAL(ap->a_vp);

	if (pt->pt_arg) {
		kfree((caddr_t) pt->pt_arg, M_TEMP);
		pt->pt_arg = 0;
	}
	kfree(ap->a_vp->v_data, M_TEMP);
	ap->a_vp->v_data = 0;

	return (0);
}


/*
 * Print out the contents of a Portal vnode.
 *
 * portal_print(struct vnode *a_vp)
 */
/* ARGSUSED */
static int
portal_print(struct vop_print_args *ap)
{
	kprintf("tag VT_PORTAL, portal vnode\n");
	return (0);
}


struct vop_ops portal_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		VOP_NULL,
	.vop_bmap =		VOP_PANIC,
	.vop_getattr =		portal_getattr,
	.vop_inactive =		portal_inactive,
	.vop_old_lookup =	portal_lookup,
	.vop_open =		portal_open,
	.vop_pathconf =		vop_stdpathconf,
	.vop_print =		portal_print,
	.vop_readdir =		portal_readdir,
	.vop_reclaim =		portal_reclaim,
	.vop_setattr =		portal_setattr
};
