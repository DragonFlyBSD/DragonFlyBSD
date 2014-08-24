/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey Hsu.
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
 *
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)kern_descrip.c	8.6 (Berkeley) 4/19/94
 * $FreeBSD: src/sys/kern/kern_descrip.c,v 1.81.2.19 2004/02/28 00:43:31 tegge Exp $
 */

#include "opt_compat.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/stat.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/unistd.h>
#include <sys/resourcevar.h>
#include <sys/event.h>
#include <sys/kern_syscall.h>
#include <sys/kcore.h>
#include <sys/kinfo.h>
#include <sys/un.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <sys/thread2.h>
#include <sys/file2.h>
#include <sys/spinlock2.h>

static void fsetfd_locked(struct filedesc *fdp, struct file *fp, int fd);
static void fdreserve_locked (struct filedesc *fdp, int fd0, int incr);
static struct file *funsetfd_locked (struct filedesc *fdp, int fd);
static void ffree(struct file *fp);

static MALLOC_DEFINE(M_FILEDESC, "file desc", "Open file descriptor table");
static MALLOC_DEFINE(M_FILEDESC_TO_LEADER, "file desc to leader",
		     "file desc to leader structures");
MALLOC_DEFINE(M_FILE, "file", "Open file structure");
static MALLOC_DEFINE(M_SIGIO, "sigio", "sigio structures");

static struct krate krate_uidinfo = { .freq = 1 };

static	 d_open_t  fdopen;
#define NUMFDESC 64

#define CDEV_MAJOR 22
static struct dev_ops fildesc_ops = {
	{ "FD", 0, 0 },
	.d_open =	fdopen,
};

/*
 * Descriptor management.
 */
static struct filelist filehead = LIST_HEAD_INITIALIZER(&filehead);
static struct spinlock filehead_spin = SPINLOCK_INITIALIZER(&filehead_spin, "filehead_spin");
static int nfiles;		/* actual number of open files */
extern int cmask;	

/*
 * Fixup fd_freefile and fd_lastfile after a descriptor has been cleared.
 *
 * MPSAFE - must be called with fdp->fd_spin exclusively held
 */
static __inline
void
fdfixup_locked(struct filedesc *fdp, int fd)
{
	if (fd < fdp->fd_freefile) {
	       fdp->fd_freefile = fd;
	}
	while (fdp->fd_lastfile >= 0 &&
	       fdp->fd_files[fdp->fd_lastfile].fp == NULL &&
	       fdp->fd_files[fdp->fd_lastfile].reserved == 0
	) {
		--fdp->fd_lastfile;
	}
}

/*
 * System calls on descriptors.
 *
 * MPSAFE
 */
int
sys_getdtablesize(struct getdtablesize_args *uap) 
{
	struct proc *p = curproc;
	struct plimit *limit = p->p_limit;
	int dtsize;

	spin_lock(&limit->p_spin);
	if (limit->pl_rlimit[RLIMIT_NOFILE].rlim_cur > INT_MAX)
		dtsize = INT_MAX;
	else
		dtsize = (int)limit->pl_rlimit[RLIMIT_NOFILE].rlim_cur;
	spin_unlock(&limit->p_spin);

	if (dtsize > maxfilesperproc)
		dtsize = maxfilesperproc;
	if (dtsize < minfilesperproc)
		dtsize = minfilesperproc;
	if (p->p_ucred->cr_uid && dtsize > maxfilesperuser)
		dtsize = maxfilesperuser;
	uap->sysmsg_result = dtsize;
	return (0);
}

/*
 * Duplicate a file descriptor to a particular value.
 *
 * note: keep in mind that a potential race condition exists when closing
 * descriptors from a shared descriptor table (via rfork).
 *
 * MPSAFE
 */
int
sys_dup2(struct dup2_args *uap)
{
	int error;
	int fd = 0;

	error = kern_dup(DUP_FIXED, uap->from, uap->to, &fd);
	uap->sysmsg_fds[0] = fd;

	return (error);
}

/*
 * Duplicate a file descriptor.
 *
 * MPSAFE
 */
int
sys_dup(struct dup_args *uap)
{
	int error;
	int fd = 0;

	error = kern_dup(DUP_VARIABLE, uap->fd, 0, &fd);
	uap->sysmsg_fds[0] = fd;

	return (error);
}

/*
 * MPALMOSTSAFE - acquires mplock for fp operations
 */
int
kern_fcntl(int fd, int cmd, union fcntl_dat *dat, struct ucred *cred)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct vnode *vp;
	u_int newmin;
	u_int oflags;
	u_int nflags;
	int tmp, error, flg = F_POSIX;

	KKASSERT(p);

	/*
	 * Operations on file descriptors that do not require a file pointer.
	 */
	switch (cmd) {
	case F_GETFD:
		error = fgetfdflags(p->p_fd, fd, &tmp);
		if (error == 0)
			dat->fc_cloexec = (tmp & UF_EXCLOSE) ? FD_CLOEXEC : 0;
		return (error);

	case F_SETFD:
		if (dat->fc_cloexec & FD_CLOEXEC)
			error = fsetfdflags(p->p_fd, fd, UF_EXCLOSE);
		else
			error = fclrfdflags(p->p_fd, fd, UF_EXCLOSE);
		return (error);
	case F_DUPFD:
		newmin = dat->fc_fd;
		error = kern_dup(DUP_VARIABLE | DUP_FCNTL, fd, newmin,
		    &dat->fc_fd);
		return (error);
	case F_DUPFD_CLOEXEC:
		newmin = dat->fc_fd;
		error = kern_dup(DUP_VARIABLE | DUP_CLOEXEC | DUP_FCNTL,
		    fd, newmin, &dat->fc_fd);
		return (error);
	case F_DUP2FD:
		newmin = dat->fc_fd;
		error = kern_dup(DUP_FIXED, fd, newmin, &dat->fc_fd);
		return (error);
	case F_DUP2FD_CLOEXEC:
		newmin = dat->fc_fd;
		error = kern_dup(DUP_FIXED | DUP_CLOEXEC, fd, newmin,
				 &dat->fc_fd);
		return (error);
	default:
		break;
	}

	/*
	 * Operations on file pointers
	 */
	if ((fp = holdfp(p->p_fd, fd, -1)) == NULL)
		return (EBADF);

	switch (cmd) {
	case F_GETFL:
		dat->fc_flags = OFLAGS(fp->f_flag);
		error = 0;
		break;

	case F_SETFL:
		oflags = fp->f_flag;
		nflags = FFLAGS(dat->fc_flags & ~O_ACCMODE) & FCNTLFLAGS;
		nflags |= oflags & ~FCNTLFLAGS;

		error = 0;
		if (((nflags ^ oflags) & O_APPEND) && (oflags & FAPPENDONLY))
			error = EINVAL;
		if (error == 0 && ((nflags ^ oflags) & FASYNC)) {
			tmp = nflags & FASYNC;
			error = fo_ioctl(fp, FIOASYNC, (caddr_t)&tmp,
					 cred, NULL);
		}
		if (error == 0)
			fp->f_flag = nflags;
		break;

	case F_GETOWN:
		error = fo_ioctl(fp, FIOGETOWN, (caddr_t)&dat->fc_owner,
				 cred, NULL);
		break;

	case F_SETOWN:
		error = fo_ioctl(fp, FIOSETOWN, (caddr_t)&dat->fc_owner,
				 cred, NULL);
		break;

	case F_SETLKW:
		flg |= F_WAIT;
		/* Fall into F_SETLK */

	case F_SETLK:
		if (fp->f_type != DTYPE_VNODE) {
			error = EBADF;
			break;
		}
		vp = (struct vnode *)fp->f_data;

		/*
		 * copyin/lockop may block
		 */
		if (dat->fc_flock.l_whence == SEEK_CUR)
			dat->fc_flock.l_start += fp->f_offset;

		switch (dat->fc_flock.l_type) {
		case F_RDLCK:
			if ((fp->f_flag & FREAD) == 0) {
				error = EBADF;
				break;
			}
			if ((p->p_leader->p_flags & P_ADVLOCK) == 0) {
				lwkt_gettoken(&p->p_leader->p_token);
				p->p_leader->p_flags |= P_ADVLOCK;
				lwkt_reltoken(&p->p_leader->p_token);
			}
			error = VOP_ADVLOCK(vp, (caddr_t)p->p_leader, F_SETLK,
			    &dat->fc_flock, flg);
			break;
		case F_WRLCK:
			if ((fp->f_flag & FWRITE) == 0) {
				error = EBADF;
				break;
			}
			if ((p->p_leader->p_flags & P_ADVLOCK) == 0) {
				lwkt_gettoken(&p->p_leader->p_token);
				p->p_leader->p_flags |= P_ADVLOCK;
				lwkt_reltoken(&p->p_leader->p_token);
			}
			error = VOP_ADVLOCK(vp, (caddr_t)p->p_leader, F_SETLK,
			    &dat->fc_flock, flg);
			break;
		case F_UNLCK:
			error = VOP_ADVLOCK(vp, (caddr_t)p->p_leader, F_UNLCK,
				&dat->fc_flock, F_POSIX);
			break;
		default:
			error = EINVAL;
			break;
		}

		/*
		 * It is possible to race a close() on the descriptor while
		 * we were blocked getting the lock.  If this occurs the
		 * close might not have caught the lock.
		 */
		if (checkfdclosed(p->p_fd, fd, fp)) {
			dat->fc_flock.l_whence = SEEK_SET;
			dat->fc_flock.l_start = 0;
			dat->fc_flock.l_len = 0;
			dat->fc_flock.l_type = F_UNLCK;
			(void) VOP_ADVLOCK(vp, (caddr_t)p->p_leader,
					   F_UNLCK, &dat->fc_flock, F_POSIX);
		}
		break;

	case F_GETLK:
		if (fp->f_type != DTYPE_VNODE) {
			error = EBADF;
			break;
		}
		vp = (struct vnode *)fp->f_data;
		/*
		 * copyin/lockop may block
		 */
		if (dat->fc_flock.l_type != F_RDLCK &&
		    dat->fc_flock.l_type != F_WRLCK &&
		    dat->fc_flock.l_type != F_UNLCK) {
			error = EINVAL;
			break;
		}
		if (dat->fc_flock.l_whence == SEEK_CUR)
			dat->fc_flock.l_start += fp->f_offset;
		error = VOP_ADVLOCK(vp, (caddr_t)p->p_leader, F_GETLK,
			    &dat->fc_flock, F_POSIX);
		break;
	default:
		error = EINVAL;
		break;
	}

	fdrop(fp);
	return (error);
}

/*
 * The file control system call.
 *
 * MPSAFE
 */
int
sys_fcntl(struct fcntl_args *uap)
{
	union fcntl_dat dat;
	int error;

	switch (uap->cmd) {
	case F_DUPFD:
	case F_DUP2FD:
	case F_DUPFD_CLOEXEC:
	case F_DUP2FD_CLOEXEC:
		dat.fc_fd = uap->arg;
		break;
	case F_SETFD:
		dat.fc_cloexec = uap->arg;
		break;
	case F_SETFL:
		dat.fc_flags = uap->arg;
		break;
	case F_SETOWN:
		dat.fc_owner = uap->arg;
		break;
	case F_SETLKW:
	case F_SETLK:
	case F_GETLK:
		error = copyin((caddr_t)uap->arg, &dat.fc_flock,
			       sizeof(struct flock));
		if (error)
			return (error);
		break;
	}

	error = kern_fcntl(uap->fd, uap->cmd, &dat, curthread->td_ucred);

	if (error == 0) {
		switch (uap->cmd) {
		case F_DUPFD:
		case F_DUP2FD:
		case F_DUPFD_CLOEXEC:
		case F_DUP2FD_CLOEXEC:
			uap->sysmsg_result = dat.fc_fd;
			break;
		case F_GETFD:
			uap->sysmsg_result = dat.fc_cloexec;
			break;
		case F_GETFL:
			uap->sysmsg_result = dat.fc_flags;
			break;
		case F_GETOWN:
			uap->sysmsg_result = dat.fc_owner;
			break;
		case F_GETLK:
			error = copyout(&dat.fc_flock, (caddr_t)uap->arg,
			    sizeof(struct flock));
			break;
		}
	}

	return (error);
}

/*
 * Common code for dup, dup2, and fcntl(F_DUPFD).
 *
 * There are four type flags: DUP_FCNTL, DUP_FIXED, DUP_VARIABLE, and
 * DUP_CLOEXEC.
 *
 * DUP_FCNTL is for handling EINVAL vs. EBADF differences between
 * fcntl()'s F_DUPFD and F_DUPFD_CLOEXEC and dup2() (per POSIX).
 * The next two flags are mutually exclusive, and the fourth is optional.
 * DUP_FIXED tells kern_dup() to destructively dup over an existing file
 * descriptor if "new" is already open.  DUP_VARIABLE tells kern_dup()
 * to find the lowest unused file descriptor that is greater than or
 * equal to "new".  DUP_CLOEXEC, which works with either of the first
 * two flags, sets the close-on-exec flag on the "new" file descriptor.
 *
 * MPSAFE
 */
int
kern_dup(int flags, int old, int new, int *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct file *delfp;
	int oldflags;
	int holdleaders;
	int dtsize;
	int error, newfd;

	/*
	 * Verify that we have a valid descriptor to dup from and
	 * possibly to dup to. When the new descriptor is out of
	 * bounds, fcntl()'s F_DUPFD and F_DUPFD_CLOEXEC must
	 * return EINVAL, while dup2() returns EBADF in
	 * this case.
	 *
	 * NOTE: maxfilesperuser is not applicable to dup()
	 */
retry:
	if (p->p_rlimit[RLIMIT_NOFILE].rlim_cur > INT_MAX)
		dtsize = INT_MAX;
	else
		dtsize = (int)p->p_rlimit[RLIMIT_NOFILE].rlim_cur;
	if (dtsize > maxfilesperproc)
		dtsize = maxfilesperproc;
	if (dtsize < minfilesperproc)
		dtsize = minfilesperproc;

	if (new < 0 || new > dtsize)
		return (flags & DUP_FCNTL ? EINVAL : EBADF);

	spin_lock(&fdp->fd_spin);
	if ((unsigned)old >= fdp->fd_nfiles || fdp->fd_files[old].fp == NULL) {
		spin_unlock(&fdp->fd_spin);
		return (EBADF);
	}
	if ((flags & DUP_FIXED) && old == new) {
		*res = new;
		if (flags & DUP_CLOEXEC)
			fdp->fd_files[new].fileflags |= UF_EXCLOSE;
		spin_unlock(&fdp->fd_spin);
		return (0);
	}
	fp = fdp->fd_files[old].fp;
	oldflags = fdp->fd_files[old].fileflags;
	fhold(fp);	/* MPSAFE - can be called with a spinlock held */

	/*
	 * Allocate a new descriptor if DUP_VARIABLE, or expand the table
	 * if the requested descriptor is beyond the current table size.
	 *
	 * This can block.  Retry if the source descriptor no longer matches
	 * or if our expectation in the expansion case races.
	 *
	 * If we are not expanding or allocating a new decriptor, then reset
	 * the target descriptor to a reserved state so we have a uniform
	 * setup for the next code block.
	 */
	if ((flags & DUP_VARIABLE) || new >= fdp->fd_nfiles) {
		spin_unlock(&fdp->fd_spin);
		error = fdalloc(p, new, &newfd);
		spin_lock(&fdp->fd_spin);
		if (error) {
			spin_unlock(&fdp->fd_spin);
			fdrop(fp);
			return (error);
		}
		/*
		 * Check for ripout
		 */
		if (old >= fdp->fd_nfiles || fdp->fd_files[old].fp != fp) {
			fsetfd_locked(fdp, NULL, newfd);
			spin_unlock(&fdp->fd_spin);
			fdrop(fp);
			goto retry;
		}
		/*
		 * Check for expansion race
		 */
		if ((flags & DUP_VARIABLE) == 0 && new != newfd) {
			fsetfd_locked(fdp, NULL, newfd);
			spin_unlock(&fdp->fd_spin);
			fdrop(fp);
			goto retry;
		}
		/*
		 * Check for ripout, newfd reused old (this case probably
		 * can't occur).
		 */
		if (old == newfd) {
			fsetfd_locked(fdp, NULL, newfd);
			spin_unlock(&fdp->fd_spin);
			fdrop(fp);
			goto retry;
		}
		new = newfd;
		delfp = NULL;
	} else {
		if (fdp->fd_files[new].reserved) {
			spin_unlock(&fdp->fd_spin);
			fdrop(fp);
			kprintf("Warning: dup(): target descriptor %d is reserved, waiting for it to be resolved\n", new);
			tsleep(fdp, 0, "fdres", hz);
			goto retry;
		}

		/*
		 * If the target descriptor was never allocated we have
		 * to allocate it.  If it was we have to clean out the
		 * old descriptor.  delfp inherits the ref from the 
		 * descriptor table.
		 */
		delfp = fdp->fd_files[new].fp;
		fdp->fd_files[new].fp = NULL;
		fdp->fd_files[new].reserved = 1;
		if (delfp == NULL) {
			fdreserve_locked(fdp, new, 1);
			if (new > fdp->fd_lastfile)
				fdp->fd_lastfile = new;
		}

	}

	/*
	 * NOTE: still holding an exclusive spinlock
	 */

	/*
	 * If a descriptor is being overwritten we may hve to tell 
	 * fdfree() to sleep to ensure that all relevant process
	 * leaders can be traversed in closef().
	 */
	if (delfp != NULL && p->p_fdtol != NULL) {
		fdp->fd_holdleaderscount++;
		holdleaders = 1;
	} else {
		holdleaders = 0;
	}
	KASSERT(delfp == NULL || (flags & DUP_FIXED),
		("dup() picked an open file"));

	/*
	 * Duplicate the source descriptor, update lastfile.  If the new
	 * descriptor was not allocated and we aren't replacing an existing
	 * descriptor we have to mark the descriptor as being in use.
	 *
	 * The fd_files[] array inherits fp's hold reference.
	 */
	fsetfd_locked(fdp, fp, new);
	if ((flags & DUP_CLOEXEC) != 0)
		fdp->fd_files[new].fileflags = oldflags | UF_EXCLOSE;
	else
		fdp->fd_files[new].fileflags = oldflags & ~UF_EXCLOSE;
	spin_unlock(&fdp->fd_spin);
	fdrop(fp);
	*res = new;

	/*
	 * If we dup'd over a valid file, we now own the reference to it
	 * and must dispose of it using closef() semantics (as if a
	 * close() were performed on it).
	 */
	if (delfp) {
		if (SLIST_FIRST(&delfp->f_klist))
			knote_fdclose(delfp, fdp, new);
		closef(delfp, p);
		if (holdleaders) {
			spin_lock(&fdp->fd_spin);
			fdp->fd_holdleaderscount--;
			if (fdp->fd_holdleaderscount == 0 &&
			    fdp->fd_holdleaderswakeup != 0) {
				fdp->fd_holdleaderswakeup = 0;
				spin_unlock(&fdp->fd_spin);
				wakeup(&fdp->fd_holdleaderscount);
			} else {
				spin_unlock(&fdp->fd_spin);
			}
		}
	}
	return (0);
}

/*
 * If sigio is on the list associated with a process or process group,
 * disable signalling from the device, remove sigio from the list and
 * free sigio.
 *
 * MPSAFE
 */
void
funsetown(struct sigio **sigiop)
{
	struct pgrp *pgrp;
	struct proc *p;
	struct sigio *sigio;

	if ((sigio = *sigiop) != NULL) {
		lwkt_gettoken(&sigio_token);	/* protect sigio */
		KKASSERT(sigiop == sigio->sio_myref);
		sigio = *sigiop;
		*sigiop = NULL;
		lwkt_reltoken(&sigio_token);
	}
	if (sigio == NULL)
		return;

	if (sigio->sio_pgid < 0) {
		pgrp = sigio->sio_pgrp;
		sigio->sio_pgrp = NULL;
		lwkt_gettoken(&pgrp->pg_token);
		SLIST_REMOVE(&pgrp->pg_sigiolst, sigio, sigio, sio_pgsigio);
		lwkt_reltoken(&pgrp->pg_token);
		pgrel(pgrp);
	} else /* if ((*sigiop)->sio_pgid > 0) */ {
		p = sigio->sio_proc;
		sigio->sio_proc = NULL;
		PHOLD(p);
		lwkt_gettoken(&p->p_token);
		SLIST_REMOVE(&p->p_sigiolst, sigio, sigio, sio_pgsigio);
		lwkt_reltoken(&p->p_token);
		PRELE(p);
	}
	crfree(sigio->sio_ucred);
	sigio->sio_ucred = NULL;
	kfree(sigio, M_SIGIO);
}

/*
 * Free a list of sigio structures.  Caller is responsible for ensuring
 * that the list is MPSAFE.
 *
 * MPSAFE
 */
void
funsetownlst(struct sigiolst *sigiolst)
{
	struct sigio *sigio;

	while ((sigio = SLIST_FIRST(sigiolst)) != NULL)
		funsetown(sigio->sio_myref);
}

/*
 * This is common code for FIOSETOWN ioctl called by fcntl(fd, F_SETOWN, arg).
 *
 * After permission checking, add a sigio structure to the sigio list for
 * the process or process group.
 *
 * MPSAFE
 */
int
fsetown(pid_t pgid, struct sigio **sigiop)
{
	struct proc *proc = NULL;
	struct pgrp *pgrp = NULL;
	struct sigio *sigio;
	int error;

	if (pgid == 0) {
		funsetown(sigiop);
		return (0);
	}

	if (pgid > 0) {
		proc = pfind(pgid);
		if (proc == NULL) {
			error = ESRCH;
			goto done;
		}

		/*
		 * Policy - Don't allow a process to FSETOWN a process
		 * in another session.
		 *
		 * Remove this test to allow maximum flexibility or
		 * restrict FSETOWN to the current process or process
		 * group for maximum safety.
		 */
		if (proc->p_session != curproc->p_session) {
			error = EPERM;
			goto done;
		}
	} else /* if (pgid < 0) */ {
		pgrp = pgfind(-pgid);
		if (pgrp == NULL) {
			error = ESRCH;
			goto done;
		}

		/*
		 * Policy - Don't allow a process to FSETOWN a process
		 * in another session.
		 *
		 * Remove this test to allow maximum flexibility or
		 * restrict FSETOWN to the current process or process
		 * group for maximum safety.
		 */
		if (pgrp->pg_session != curproc->p_session) {
			error = EPERM;
			goto done;
		}
	}
	sigio = kmalloc(sizeof(struct sigio), M_SIGIO, M_WAITOK | M_ZERO);
	if (pgid > 0) {
		KKASSERT(pgrp == NULL);
		lwkt_gettoken(&proc->p_token);
		SLIST_INSERT_HEAD(&proc->p_sigiolst, sigio, sio_pgsigio);
		sigio->sio_proc = proc;
		lwkt_reltoken(&proc->p_token);
	} else {
		KKASSERT(proc == NULL);
		lwkt_gettoken(&pgrp->pg_token);
		SLIST_INSERT_HEAD(&pgrp->pg_sigiolst, sigio, sio_pgsigio);
		sigio->sio_pgrp = pgrp;
		lwkt_reltoken(&pgrp->pg_token);
		pgrp = NULL;
	}
	sigio->sio_pgid = pgid;
	sigio->sio_ucred = crhold(curthread->td_ucred);
	/* It would be convenient if p_ruid was in ucred. */
	sigio->sio_ruid = sigio->sio_ucred->cr_ruid;
	sigio->sio_myref = sigiop;

	lwkt_gettoken(&sigio_token);
	while (*sigiop)
		funsetown(sigiop);
	*sigiop = sigio;
	lwkt_reltoken(&sigio_token);
	error = 0;
done:
	if (pgrp)
		pgrel(pgrp);
	if (proc)
		PRELE(proc);
	return (error);
}

/*
 * This is common code for FIOGETOWN ioctl called by fcntl(fd, F_GETOWN, arg).
 *
 * MPSAFE
 */
pid_t
fgetown(struct sigio **sigiop)
{
	struct sigio *sigio;
	pid_t own;

	lwkt_gettoken_shared(&sigio_token);
	sigio = *sigiop;
	own = (sigio != NULL ? sigio->sio_pgid : 0);
	lwkt_reltoken(&sigio_token);

	return (own);
}

/*
 * Close many file descriptors.
 *
 * MPSAFE
 */
int
sys_closefrom(struct closefrom_args *uap)
{
	return(kern_closefrom(uap->fd));
}

/*
 * Close all file descriptors greater then or equal to fd
 *
 * MPSAFE
 */
int
kern_closefrom(int fd)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp;

	KKASSERT(p);
	fdp = p->p_fd;

	if (fd < 0)
		return (EINVAL);

	/*
	 * NOTE: This function will skip unassociated descriptors and
	 * reserved descriptors that have not yet been assigned.  
	 * fd_lastfile can change as a side effect of kern_close().
	 */
	spin_lock(&fdp->fd_spin);
	while (fd <= fdp->fd_lastfile) {
		if (fdp->fd_files[fd].fp != NULL) {
			spin_unlock(&fdp->fd_spin);
			/* ok if this races another close */
			if (kern_close(fd) == EINTR)
				return (EINTR);
			spin_lock(&fdp->fd_spin);
		}
		++fd;
	}
	spin_unlock(&fdp->fd_spin);
	return (0);
}

/*
 * Close a file descriptor.
 *
 * MPSAFE
 */
int
sys_close(struct close_args *uap)
{
	return(kern_close(uap->fd));
}

/*
 * MPSAFE
 */
int
kern_close(int fd)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp;
	struct file *fp;
	int error;
	int holdleaders;

	KKASSERT(p);
	fdp = p->p_fd;

	spin_lock(&fdp->fd_spin);
	if ((fp = funsetfd_locked(fdp, fd)) == NULL) {
		spin_unlock(&fdp->fd_spin);
		return (EBADF);
	}
	holdleaders = 0;
	if (p->p_fdtol != NULL) {
		/*
		 * Ask fdfree() to sleep to ensure that all relevant
		 * process leaders can be traversed in closef().
		 */
		fdp->fd_holdleaderscount++;
		holdleaders = 1;
	}

	/*
	 * we now hold the fp reference that used to be owned by the descriptor
	 * array.
	 */
	spin_unlock(&fdp->fd_spin);
	if (SLIST_FIRST(&fp->f_klist))
		knote_fdclose(fp, fdp, fd);
	error = closef(fp, p);
	if (holdleaders) {
		spin_lock(&fdp->fd_spin);
		fdp->fd_holdleaderscount--;
		if (fdp->fd_holdleaderscount == 0 &&
		    fdp->fd_holdleaderswakeup != 0) {
			fdp->fd_holdleaderswakeup = 0;
			spin_unlock(&fdp->fd_spin);
			wakeup(&fdp->fd_holdleaderscount);
		} else {
			spin_unlock(&fdp->fd_spin);
		}
	}
	return (error);
}

/*
 * shutdown_args(int fd, int how)
 */
int
kern_shutdown(int fd, int how)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	KKASSERT(p);

	if ((fp = holdfp(p->p_fd, fd, -1)) == NULL)
		return (EBADF);
	error = fo_shutdown(fp, how);
	fdrop(fp);

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_shutdown(struct shutdown_args *uap)
{
	int error;

	error = kern_shutdown(uap->s, uap->how);

	return (error);
}

/*
 * MPSAFE
 */
int
kern_fstat(int fd, struct stat *ub)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	KKASSERT(p);

	if ((fp = holdfp(p->p_fd, fd, -1)) == NULL)
		return (EBADF);
	error = fo_stat(fp, ub, td->td_ucred);
	fdrop(fp);

	return (error);
}

/*
 * Return status information about a file descriptor.
 *
 * MPSAFE
 */
int
sys_fstat(struct fstat_args *uap)
{
	struct stat st;
	int error;

	error = kern_fstat(uap->fd, &st);

	if (error == 0)
		error = copyout(&st, uap->sb, sizeof(st));
	return (error);
}

/*
 * Return pathconf information about a file descriptor.
 *
 * MPALMOSTSAFE
 */
int
sys_fpathconf(struct fpathconf_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct vnode *vp;
	int error = 0;

	if ((fp = holdfp(p->p_fd, uap->fd, -1)) == NULL)
		return (EBADF);

	switch (fp->f_type) {
	case DTYPE_PIPE:
	case DTYPE_SOCKET:
		if (uap->name != _PC_PIPE_BUF) {
			error = EINVAL;
		} else {
			uap->sysmsg_result = PIPE_BUF;
			error = 0;
		}
		break;
	case DTYPE_FIFO:
	case DTYPE_VNODE:
		vp = (struct vnode *)fp->f_data;
		error = VOP_PATHCONF(vp, uap->name, &uap->sysmsg_reg);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	fdrop(fp);
	return(error);
}

static int fdexpand;
SYSCTL_INT(_debug, OID_AUTO, fdexpand, CTLFLAG_RD, &fdexpand, 0,
    "Number of times a file table has been expanded");

/*
 * Grow the file table so it can hold through descriptor (want).
 *
 * The fdp's spinlock must be held exclusively on entry and may be held
 * exclusively on return.  The spinlock may be cycled by the routine.
 *
 * MPSAFE
 */
static void
fdgrow_locked(struct filedesc *fdp, int want)
{
	struct fdnode *newfiles;
	struct fdnode *oldfiles;
	int nf, extra;

	nf = fdp->fd_nfiles;
	do {
		/* nf has to be of the form 2^n - 1 */
		nf = 2 * nf + 1;
	} while (nf <= want);

	spin_unlock(&fdp->fd_spin);
	newfiles = kmalloc(nf * sizeof(struct fdnode), M_FILEDESC, M_WAITOK);
	spin_lock(&fdp->fd_spin);

	/*
	 * We could have raced another extend while we were not holding
	 * the spinlock.
	 */
	if (fdp->fd_nfiles >= nf) {
		spin_unlock(&fdp->fd_spin);
		kfree(newfiles, M_FILEDESC);
		spin_lock(&fdp->fd_spin);
		return;
	}
	/*
	 * Copy the existing ofile and ofileflags arrays
	 * and zero the new portion of each array.
	 */
	extra = nf - fdp->fd_nfiles;
	bcopy(fdp->fd_files, newfiles, fdp->fd_nfiles * sizeof(struct fdnode));
	bzero(&newfiles[fdp->fd_nfiles], extra * sizeof(struct fdnode));

	oldfiles = fdp->fd_files;
	fdp->fd_files = newfiles;
	fdp->fd_nfiles = nf;

	if (oldfiles != fdp->fd_builtin_files) {
		spin_unlock(&fdp->fd_spin);
		kfree(oldfiles, M_FILEDESC);
		spin_lock(&fdp->fd_spin);
	}
	fdexpand++;
}

/*
 * Number of nodes in right subtree, including the root.
 */
static __inline int
right_subtree_size(int n)
{
	return (n ^ (n | (n + 1)));
}

/*
 * Bigger ancestor.
 */
static __inline int
right_ancestor(int n)
{
	return (n | (n + 1));
}

/*
 * Smaller ancestor.
 */
static __inline int
left_ancestor(int n)
{
	return ((n & (n + 1)) - 1);
}

/*
 * Traverse the in-place binary tree buttom-up adjusting the allocation
 * count so scans can determine where free descriptors are located.
 *
 * MPSAFE - caller must be holding an exclusive spinlock on fdp
 */
static
void
fdreserve_locked(struct filedesc *fdp, int fd, int incr)
{
	while (fd >= 0) {
		fdp->fd_files[fd].allocated += incr;
		KKASSERT(fdp->fd_files[fd].allocated >= 0);
		fd = left_ancestor(fd);
	}
}

/*
 * Reserve a file descriptor for the process.  If no error occurs, the
 * caller MUST at some point call fsetfd() or assign a file pointer
 * or dispose of the reservation.
 *
 * MPSAFE
 */
int
fdalloc(struct proc *p, int want, int *result)
{
	struct filedesc *fdp = p->p_fd;
	struct uidinfo *uip;
	int fd, rsize, rsum, node, lim;

	/*
	 * Check dtable size limit
	 */
	spin_lock(&p->p_limit->p_spin);
	if (p->p_rlimit[RLIMIT_NOFILE].rlim_cur > INT_MAX)
		lim = INT_MAX;
	else
		lim = (int)p->p_rlimit[RLIMIT_NOFILE].rlim_cur;
	spin_unlock(&p->p_limit->p_spin);

	if (lim > maxfilesperproc)
		lim = maxfilesperproc;
	if (lim < minfilesperproc)
		lim = minfilesperproc;
	if (want >= lim)
		return (EMFILE);

	/*
	 * Check that the user has not run out of descriptors (non-root only).
	 * As a safety measure the dtable is allowed to have at least
	 * minfilesperproc open fds regardless of the maxfilesperuser limit.
	 */
	if (p->p_ucred->cr_uid && fdp->fd_nfiles >= minfilesperproc) {
		uip = p->p_ucred->cr_uidinfo;
		if (uip->ui_openfiles > maxfilesperuser) {
			krateprintf(&krate_uidinfo,
				    "Warning: user %d pid %d (%s) ran out of "
				    "file descriptors (%d/%d)\n",
				    p->p_ucred->cr_uid, (int)p->p_pid,
				    p->p_comm,
				    uip->ui_openfiles, maxfilesperuser);
			return(ENFILE);
		}
	}

	/*
	 * Grow the dtable if necessary
	 */
	spin_lock(&fdp->fd_spin);
	if (want >= fdp->fd_nfiles)
		fdgrow_locked(fdp, want);

	/*
	 * Search for a free descriptor starting at the higher
	 * of want or fd_freefile.  If that fails, consider
	 * expanding the ofile array.
	 *
	 * NOTE! the 'allocated' field is a cumulative recursive allocation
	 * count.  If we happen to see a value of 0 then we can shortcut
	 * our search.  Otherwise we run through through the tree going
	 * down branches we know have free descriptor(s) until we hit a
	 * leaf node.  The leaf node will be free but will not necessarily
	 * have an allocated field of 0.
	 */
retry:
	/* move up the tree looking for a subtree with a free node */
	for (fd = max(want, fdp->fd_freefile); fd < min(fdp->fd_nfiles, lim);
	     fd = right_ancestor(fd)) {
		if (fdp->fd_files[fd].allocated == 0)
			goto found;

		rsize = right_subtree_size(fd);
		if (fdp->fd_files[fd].allocated == rsize)
			continue;	/* right subtree full */

		/*
		 * Free fd is in the right subtree of the tree rooted at fd.
		 * Call that subtree R.  Look for the smallest (leftmost)
		 * subtree of R with an unallocated fd: continue moving
		 * down the left branch until encountering a full left
		 * subtree, then move to the right.
		 */
		for (rsum = 0, rsize /= 2; rsize > 0; rsize /= 2) {
			node = fd + rsize;
			rsum += fdp->fd_files[node].allocated;
			if (fdp->fd_files[fd].allocated == rsum + rsize) {
				fd = node;	/* move to the right */
				if (fdp->fd_files[node].allocated == 0)
					goto found;
				rsum = 0;
			}
		}
		goto found;
	}

	/*
	 * No space in current array.  Expand?
	 */
	if (fdp->fd_nfiles >= lim) {
		spin_unlock(&fdp->fd_spin);
		return (EMFILE);
	}
	fdgrow_locked(fdp, want);
	goto retry;

found:
	KKASSERT(fd < fdp->fd_nfiles);
	if (fd > fdp->fd_lastfile)
		fdp->fd_lastfile = fd;
	if (want <= fdp->fd_freefile)
		fdp->fd_freefile = fd;
	*result = fd;
	KKASSERT(fdp->fd_files[fd].fp == NULL);
	KKASSERT(fdp->fd_files[fd].reserved == 0);
	fdp->fd_files[fd].fileflags = 0;
	fdp->fd_files[fd].reserved = 1;
	fdreserve_locked(fdp, fd, 1);
	spin_unlock(&fdp->fd_spin);
	return (0);
}

/*
 * Check to see whether n user file descriptors
 * are available to the process p.
 *
 * MPSAFE
 */
int
fdavail(struct proc *p, int n)
{
	struct filedesc *fdp = p->p_fd;
	struct fdnode *fdnode;
	int i, lim, last;

	spin_lock(&p->p_limit->p_spin);
	if (p->p_rlimit[RLIMIT_NOFILE].rlim_cur > INT_MAX)
		lim = INT_MAX;
	else
		lim = (int)p->p_rlimit[RLIMIT_NOFILE].rlim_cur;
	spin_unlock(&p->p_limit->p_spin);

	if (lim > maxfilesperproc)
		lim = maxfilesperproc;
	if (lim < minfilesperproc)
		lim = minfilesperproc;

	spin_lock(&fdp->fd_spin);
	if ((i = lim - fdp->fd_nfiles) > 0 && (n -= i) <= 0) {
		spin_unlock(&fdp->fd_spin);
		return (1);
	}
	last = min(fdp->fd_nfiles, lim);
	fdnode = &fdp->fd_files[fdp->fd_freefile];
	for (i = last - fdp->fd_freefile; --i >= 0; ++fdnode) {
		if (fdnode->fp == NULL && --n <= 0) {
			spin_unlock(&fdp->fd_spin);
			return (1);
		}
	}
	spin_unlock(&fdp->fd_spin);
	return (0);
}

/*
 * Revoke open descriptors referencing (f_data, f_type)
 *
 * Any revoke executed within a prison is only able to
 * revoke descriptors for processes within that prison.
 *
 * Returns 0 on success or an error code.
 */
struct fdrevoke_info {
	void *data;
	short type;
	short unused;
	int count;
	int intransit;
	struct ucred *cred;
	struct file *nfp;
};

static int fdrevoke_check_callback(struct file *fp, void *vinfo);
static int fdrevoke_proc_callback(struct proc *p, void *vinfo);

int
fdrevoke(void *f_data, short f_type, struct ucred *cred)
{
	struct fdrevoke_info info;
	int error;

	bzero(&info, sizeof(info));
	info.data = f_data;
	info.type = f_type;
	info.cred = cred;
	error = falloc(NULL, &info.nfp, NULL);
	if (error)
		return (error);

	/*
	 * Scan the file pointer table once.  dups do not dup file pointers,
	 * only descriptors, so there is no leak.  Set FREVOKED on the fps
	 * being revoked.
	 */
	allfiles_scan_exclusive(fdrevoke_check_callback, &info);

	/*
	 * If any fps were marked track down the related descriptors
	 * and close them.  Any dup()s at this point will notice
	 * the FREVOKED already set in the fp and do the right thing.
	 *
	 * Any fps with non-zero msgcounts (aka sent over a unix-domain
	 * socket) bumped the intransit counter and will require a
	 * scan.  Races against fps leaving the socket are closed by
	 * the socket code checking for FREVOKED.
	 */
	if (info.count)
		allproc_scan(fdrevoke_proc_callback, &info);
	if (info.intransit)
		unp_revoke_gc(info.nfp);
	fdrop(info.nfp);
	return(0);
}

/*
 * Locate matching file pointers directly.
 *
 * WARNING: allfiles_scan_exclusive() holds a spinlock through these calls!
 */
static int
fdrevoke_check_callback(struct file *fp, void *vinfo)
{
	struct fdrevoke_info *info = vinfo;

	/*
	 * File pointers already flagged for revokation are skipped.
	 */
	if (fp->f_flag & FREVOKED)
		return(0);

	/*
	 * If revoking from a prison file pointers created outside of
	 * that prison, or file pointers without creds, cannot be revoked.
	 */
	if (info->cred->cr_prison &&
	    (fp->f_cred == NULL ||
	     info->cred->cr_prison != fp->f_cred->cr_prison)) {
		return(0);
	}

	/*
	 * If the file pointer matches then mark it for revocation.  The
	 * flag is currently only used by unp_revoke_gc().
	 *
	 * info->count is a heuristic and can race in a SMP environment.
	 */
	if (info->data == fp->f_data && info->type == fp->f_type) {
		atomic_set_int(&fp->f_flag, FREVOKED);
		info->count += fp->f_count;
		if (fp->f_msgcount)
			++info->intransit;
	}
	return(0);
}

/*
 * Locate matching file pointers via process descriptor tables.
 */
static int
fdrevoke_proc_callback(struct proc *p, void *vinfo)
{
	struct fdrevoke_info *info = vinfo;
	struct filedesc *fdp;
	struct file *fp;
	int n;

	if (p->p_stat == SIDL || p->p_stat == SZOMB)
		return(0);
	if (info->cred->cr_prison &&
	    info->cred->cr_prison != p->p_ucred->cr_prison) {
		return(0);
	}

	/*
	 * If the controlling terminal of the process matches the
	 * vnode being revoked we clear the controlling terminal.
	 *
	 * The normal spec_close() may not catch this because it
	 * uses curproc instead of p.
	 */
	if (p->p_session && info->type == DTYPE_VNODE &&
	    info->data == p->p_session->s_ttyvp) {
		p->p_session->s_ttyvp = NULL;
		vrele(info->data);
	}

	/*
	 * Softref the fdp to prevent it from being destroyed
	 */
	spin_lock(&p->p_spin);
	if ((fdp = p->p_fd) == NULL) {
		spin_unlock(&p->p_spin);
		return(0);
	}
	atomic_add_int(&fdp->fd_softrefs, 1);
	spin_unlock(&p->p_spin);

	/*
	 * Locate and close any matching file descriptors.
	 */
	spin_lock(&fdp->fd_spin);
	for (n = 0; n < fdp->fd_nfiles; ++n) {
		if ((fp = fdp->fd_files[n].fp) == NULL)
			continue;
		if (fp->f_flag & FREVOKED) {
			fhold(info->nfp);
			fdp->fd_files[n].fp = info->nfp;
			spin_unlock(&fdp->fd_spin);
			knote_fdclose(fp, fdp, n);	/* XXX */
			closef(fp, p);
			spin_lock(&fdp->fd_spin);
			--info->count;
		}
	}
	spin_unlock(&fdp->fd_spin);
	atomic_subtract_int(&fdp->fd_softrefs, 1);
	return(0);
}

/*
 * falloc:
 *	Create a new open file structure and reserve a file decriptor
 *	for the process that refers to it.
 *
 *	Root creds are checked using lp, or assumed if lp is NULL.  If
 *	resultfd is non-NULL then lp must also be non-NULL.  No file
 *	descriptor is reserved (and no process context is needed) if
 *	resultfd is NULL.
 *
 *	A file pointer with a refcount of 1 is returned.  Note that the
 *	file pointer is NOT associated with the descriptor.  If falloc
 *	returns success, fsetfd() MUST be called to either associate the
 *	file pointer or clear the reservation.
 *
 * MPSAFE
 */
int
falloc(struct lwp *lp, struct file **resultfp, int *resultfd)
{
	static struct timeval lastfail;
	static int curfail;
	struct file *fp;
	struct ucred *cred = lp ? lp->lwp_thread->td_ucred : proc0.p_ucred;
	int error;

	fp = NULL;

	/*
	 * Handle filetable full issues and root overfill.
	 */
	if (nfiles >= maxfiles - maxfilesrootres &&
	    (cred->cr_ruid != 0 || nfiles >= maxfiles)) {
		if (ppsratecheck(&lastfail, &curfail, 1)) {
			kprintf("kern.maxfiles limit exceeded by uid %d, "
				"please see tuning(7).\n",
				cred->cr_ruid);
		}
		error = ENFILE;
		goto done;
	}

	/*
	 * Allocate a new file descriptor.
	 */
	fp = kmalloc(sizeof(struct file), M_FILE, M_WAITOK | M_ZERO);
	spin_init(&fp->f_spin, "falloc");
	SLIST_INIT(&fp->f_klist);
	fp->f_count = 1;
	fp->f_ops = &badfileops;
	fp->f_seqcount = 1;
	fsetcred(fp, cred);
	spin_lock(&filehead_spin);
	nfiles++;
	LIST_INSERT_HEAD(&filehead, fp, f_list);
	spin_unlock(&filehead_spin);
	if (resultfd) {
		if ((error = fdalloc(lp->lwp_proc, 0, resultfd)) != 0) {
			fdrop(fp);
			fp = NULL;
		}
	} else {
		error = 0;
	}
done:
	*resultfp = fp;
	return (error);
}

/*
 * Check for races against a file descriptor by determining that the
 * file pointer is still associated with the specified file descriptor,
 * and a close is not currently in progress.
 *
 * MPSAFE
 */
int
checkfdclosed(struct filedesc *fdp, int fd, struct file *fp)
{
	int error;

	spin_lock_shared(&fdp->fd_spin);
	if ((unsigned)fd >= fdp->fd_nfiles || fp != fdp->fd_files[fd].fp)
		error = EBADF;
	else
		error = 0;
	spin_unlock_shared(&fdp->fd_spin);
	return (error);
}

/*
 * Associate a file pointer with a previously reserved file descriptor.
 * This function always succeeds.
 *
 * If fp is NULL, the file descriptor is returned to the pool.
 */

/*
 * MPSAFE (exclusive spinlock must be held on call)
 */
static void
fsetfd_locked(struct filedesc *fdp, struct file *fp, int fd)
{
	KKASSERT((unsigned)fd < fdp->fd_nfiles);
	KKASSERT(fdp->fd_files[fd].reserved != 0);
	if (fp) {
		fhold(fp);
		fdp->fd_files[fd].fp = fp;
		fdp->fd_files[fd].reserved = 0;
	} else {
		fdp->fd_files[fd].reserved = 0;
		fdreserve_locked(fdp, fd, -1);
		fdfixup_locked(fdp, fd);
	}
}

/*
 * MPSAFE
 */
void
fsetfd(struct filedesc *fdp, struct file *fp, int fd)
{
	spin_lock(&fdp->fd_spin);
	fsetfd_locked(fdp, fp, fd);
	spin_unlock(&fdp->fd_spin);
}

/*
 * MPSAFE (exclusive spinlock must be held on call)
 */
static 
struct file *
funsetfd_locked(struct filedesc *fdp, int fd)
{
	struct file *fp;

	if ((unsigned)fd >= fdp->fd_nfiles)
		return (NULL);
	if ((fp = fdp->fd_files[fd].fp) == NULL)
		return (NULL);
	fdp->fd_files[fd].fp = NULL;
	fdp->fd_files[fd].fileflags = 0;

	fdreserve_locked(fdp, fd, -1);
	fdfixup_locked(fdp, fd);
	return(fp);
}

/*
 * MPSAFE
 */
int
fgetfdflags(struct filedesc *fdp, int fd, int *flagsp)
{
	int error;

	spin_lock(&fdp->fd_spin);
	if (((u_int)fd) >= fdp->fd_nfiles) {
		error = EBADF;
	} else if (fdp->fd_files[fd].fp == NULL) {
		error = EBADF;
	} else {
		*flagsp = fdp->fd_files[fd].fileflags;
		error = 0;
	}
	spin_unlock(&fdp->fd_spin);
	return (error);
}

/*
 * MPSAFE
 */
int
fsetfdflags(struct filedesc *fdp, int fd, int add_flags)
{
	int error;

	spin_lock(&fdp->fd_spin);
	if (((u_int)fd) >= fdp->fd_nfiles) {
		error = EBADF;
	} else if (fdp->fd_files[fd].fp == NULL) {
		error = EBADF;
	} else {
		fdp->fd_files[fd].fileflags |= add_flags;
		error = 0;
	}
	spin_unlock(&fdp->fd_spin);
	return (error);
}

/*
 * MPSAFE
 */
int
fclrfdflags(struct filedesc *fdp, int fd, int rem_flags)
{
	int error;

	spin_lock(&fdp->fd_spin);
	if (((u_int)fd) >= fdp->fd_nfiles) {
		error = EBADF;
	} else if (fdp->fd_files[fd].fp == NULL) {
		error = EBADF;
	} else {
		fdp->fd_files[fd].fileflags &= ~rem_flags;
		error = 0;
	}
	spin_unlock(&fdp->fd_spin);
	return (error);
}

/*
 * Set/Change/Clear the creds for a fp and synchronize the uidinfo.
 */
void
fsetcred(struct file *fp, struct ucred *ncr)
{
	struct ucred *ocr;
	struct uidinfo *uip;

	ocr = fp->f_cred;
	if (ocr == NULL || ncr == NULL || ocr->cr_uidinfo != ncr->cr_uidinfo) {
		if (ocr) {
			uip = ocr->cr_uidinfo;
			atomic_add_int(&uip->ui_openfiles, -1);
		}
		if (ncr) {
			uip = ncr->cr_uidinfo;
			atomic_add_int(&uip->ui_openfiles, 1);
		}
	}
	if (ncr)
		crhold(ncr);
	fp->f_cred = ncr;
	if (ocr)
		crfree(ocr);
}

/*
 * Free a file descriptor.
 */
static
void
ffree(struct file *fp)
{
	KASSERT((fp->f_count == 0), ("ffree: fp_fcount not 0!"));
	spin_lock(&filehead_spin);
	LIST_REMOVE(fp, f_list);
	nfiles--;
	spin_unlock(&filehead_spin);
	fsetcred(fp, NULL);
	if (fp->f_nchandle.ncp)
	    cache_drop(&fp->f_nchandle);
	kfree(fp, M_FILE);
}

/*
 * called from init_main, initialize filedesc0 for proc0.
 */
void
fdinit_bootstrap(struct proc *p0, struct filedesc *fdp0, int cmask)
{
	p0->p_fd = fdp0;
	p0->p_fdtol = NULL;
	fdp0->fd_refcnt = 1;
	fdp0->fd_cmask = cmask;
	fdp0->fd_files = fdp0->fd_builtin_files;
	fdp0->fd_nfiles = NDFILE;
	fdp0->fd_lastfile = -1;
	spin_init(&fdp0->fd_spin, "fdinitbootstrap");
}

/*
 * Build a new filedesc structure.
 *
 * NOT MPSAFE (vref)
 */
struct filedesc *
fdinit(struct proc *p)
{
	struct filedesc *newfdp;
	struct filedesc *fdp = p->p_fd;

	newfdp = kmalloc(sizeof(struct filedesc), M_FILEDESC, M_WAITOK|M_ZERO);
	spin_lock(&fdp->fd_spin);
	if (fdp->fd_cdir) {
		newfdp->fd_cdir = fdp->fd_cdir;
		vref(newfdp->fd_cdir);
		cache_copy(&fdp->fd_ncdir, &newfdp->fd_ncdir);
	}

	/*
	 * rdir may not be set in e.g. proc0 or anything vm_fork'd off of
	 * proc0, but should unconditionally exist in other processes.
	 */
	if (fdp->fd_rdir) {
		newfdp->fd_rdir = fdp->fd_rdir;
		vref(newfdp->fd_rdir);
		cache_copy(&fdp->fd_nrdir, &newfdp->fd_nrdir);
	}
	if (fdp->fd_jdir) {
		newfdp->fd_jdir = fdp->fd_jdir;
		vref(newfdp->fd_jdir);
		cache_copy(&fdp->fd_njdir, &newfdp->fd_njdir);
	}
	spin_unlock(&fdp->fd_spin);

	/* Create the file descriptor table. */
	newfdp->fd_refcnt = 1;
	newfdp->fd_cmask = cmask;
	newfdp->fd_files = newfdp->fd_builtin_files;
	newfdp->fd_nfiles = NDFILE;
	newfdp->fd_lastfile = -1;
	spin_init(&newfdp->fd_spin, "fdinit");

	return (newfdp);
}

/*
 * Share a filedesc structure.
 *
 * MPSAFE
 */
struct filedesc *
fdshare(struct proc *p)
{
	struct filedesc *fdp;

	fdp = p->p_fd;
	spin_lock(&fdp->fd_spin);
	fdp->fd_refcnt++;
	spin_unlock(&fdp->fd_spin);
	return (fdp);
}

/*
 * Copy a filedesc structure.
 *
 * MPSAFE
 */
int
fdcopy(struct proc *p, struct filedesc **fpp)
{
	struct filedesc *fdp = p->p_fd;
	struct filedesc *newfdp;
	struct fdnode *fdnode;
	int i;
	int ni;

	/*
	 * Certain daemons might not have file descriptors. 
	 */
	if (fdp == NULL)
		return (0);

	/*
	 * Allocate the new filedesc and fd_files[] array.  This can race
	 * with operations by other threads on the fdp so we have to be
	 * careful.
	 */
	newfdp = kmalloc(sizeof(struct filedesc), 
			 M_FILEDESC, M_WAITOK | M_ZERO | M_NULLOK);
	if (newfdp == NULL) {
		*fpp = NULL;
		return (-1);
	}
again:
	spin_lock(&fdp->fd_spin);
	if (fdp->fd_lastfile < NDFILE) {
		newfdp->fd_files = newfdp->fd_builtin_files;
		i = NDFILE;
	} else {
		/*
		 * We have to allocate (N^2-1) entries for our in-place
		 * binary tree.  Allow the table to shrink.
		 */
		i = fdp->fd_nfiles;
		ni = (i - 1) / 2;
		while (ni > fdp->fd_lastfile && ni > NDFILE) {
			i = ni;
			ni = (i - 1) / 2;
		}
		spin_unlock(&fdp->fd_spin);
		newfdp->fd_files = kmalloc(i * sizeof(struct fdnode),
					  M_FILEDESC, M_WAITOK | M_ZERO);

		/*
		 * Check for race, retry
		 */
		spin_lock(&fdp->fd_spin);
		if (i <= fdp->fd_lastfile) {
			spin_unlock(&fdp->fd_spin);
			kfree(newfdp->fd_files, M_FILEDESC);
			goto again;
		}
	}

	/*
	 * Dup the remaining fields. vref() and cache_hold() can be
	 * safely called while holding the read spinlock on fdp.
	 *
	 * The read spinlock on fdp is still being held.
	 *
	 * NOTE: vref and cache_hold calls for the case where the vnode
	 * or cache entry already has at least one ref may be called
	 * while holding spin locks.
	 */
	if ((newfdp->fd_cdir = fdp->fd_cdir) != NULL) {
		vref(newfdp->fd_cdir);
		cache_copy(&fdp->fd_ncdir, &newfdp->fd_ncdir);
	}
	/*
	 * We must check for fd_rdir here, at least for now because
	 * the init process is created before we have access to the
	 * rootvode to take a reference to it.
	 */
	if ((newfdp->fd_rdir = fdp->fd_rdir) != NULL) {
		vref(newfdp->fd_rdir);
		cache_copy(&fdp->fd_nrdir, &newfdp->fd_nrdir);
	}
	if ((newfdp->fd_jdir = fdp->fd_jdir) != NULL) {
		vref(newfdp->fd_jdir);
		cache_copy(&fdp->fd_njdir, &newfdp->fd_njdir);
	}
	newfdp->fd_refcnt = 1;
	newfdp->fd_nfiles = i;
	newfdp->fd_lastfile = fdp->fd_lastfile;
	newfdp->fd_freefile = fdp->fd_freefile;
	newfdp->fd_cmask = fdp->fd_cmask;
	spin_init(&newfdp->fd_spin, "fdcopy");

	/*
	 * Copy the descriptor table through (i).  This also copies the
	 * allocation state.   Then go through and ref the file pointers
	 * and clean up any KQ descriptors.
	 *
	 * kq descriptors cannot be copied.  Since we haven't ref'd the
	 * copied files yet we can ignore the return value from funsetfd().
	 *
	 * The read spinlock on fdp is still being held.
	 */
	bcopy(fdp->fd_files, newfdp->fd_files, i * sizeof(struct fdnode));
	for (i = 0 ; i < newfdp->fd_nfiles; ++i) {
		fdnode = &newfdp->fd_files[i];
		if (fdnode->reserved) {
			fdreserve_locked(newfdp, i, -1);
			fdnode->reserved = 0;
			fdfixup_locked(newfdp, i);
		} else if (fdnode->fp) {
			if (fdnode->fp->f_type == DTYPE_KQUEUE) {
				(void)funsetfd_locked(newfdp, i);
			} else {
				fhold(fdnode->fp);
			}
		}
	}
	spin_unlock(&fdp->fd_spin);
	*fpp = newfdp;
	return (0);
}

/*
 * Release a filedesc structure.
 *
 * NOT MPSAFE (MPSAFE for refs > 1, but the final cleanup code is not MPSAFE)
 */
void
fdfree(struct proc *p, struct filedesc *repl)
{
	struct filedesc *fdp;
	struct fdnode *fdnode;
	int i;
	struct filedesc_to_leader *fdtol;
	struct file *fp;
	struct vnode *vp;
	struct flock lf;

	/*
	 * Certain daemons might not have file descriptors.
	 */
	fdp = p->p_fd;
	if (fdp == NULL) {
		p->p_fd = repl;
		return;
	}

	/*
	 * Severe messing around to follow.
	 */
	spin_lock(&fdp->fd_spin);

	/* Check for special need to clear POSIX style locks */
	fdtol = p->p_fdtol;
	if (fdtol != NULL) {
		KASSERT(fdtol->fdl_refcount > 0,
			("filedesc_to_refcount botch: fdl_refcount=%d",
			 fdtol->fdl_refcount));
		if (fdtol->fdl_refcount == 1 &&
		    (p->p_leader->p_flags & P_ADVLOCK) != 0) {
			for (i = 0; i <= fdp->fd_lastfile; ++i) {
				fdnode = &fdp->fd_files[i];
				if (fdnode->fp == NULL ||
				    fdnode->fp->f_type != DTYPE_VNODE) {
					continue;
				}
				fp = fdnode->fp;
				fhold(fp);
				spin_unlock(&fdp->fd_spin);

				lf.l_whence = SEEK_SET;
				lf.l_start = 0;
				lf.l_len = 0;
				lf.l_type = F_UNLCK;
				vp = (struct vnode *)fp->f_data;
				(void) VOP_ADVLOCK(vp,
						   (caddr_t)p->p_leader,
						   F_UNLCK,
						   &lf,
						   F_POSIX);
				fdrop(fp);
				spin_lock(&fdp->fd_spin);
			}
		}
	retry:
		if (fdtol->fdl_refcount == 1) {
			if (fdp->fd_holdleaderscount > 0 &&
			    (p->p_leader->p_flags & P_ADVLOCK) != 0) {
				/*
				 * close() or do_dup() has cleared a reference
				 * in a shared file descriptor table.
				 */
				fdp->fd_holdleaderswakeup = 1;
				ssleep(&fdp->fd_holdleaderscount,
				       &fdp->fd_spin, 0, "fdlhold", 0);
				goto retry;
			}
			if (fdtol->fdl_holdcount > 0) {
				/* 
				 * Ensure that fdtol->fdl_leader
				 * remains valid in closef().
				 */
				fdtol->fdl_wakeup = 1;
				ssleep(fdtol, &fdp->fd_spin, 0, "fdlhold", 0);
				goto retry;
			}
		}
		fdtol->fdl_refcount--;
		if (fdtol->fdl_refcount == 0 &&
		    fdtol->fdl_holdcount == 0) {
			fdtol->fdl_next->fdl_prev = fdtol->fdl_prev;
			fdtol->fdl_prev->fdl_next = fdtol->fdl_next;
		} else {
			fdtol = NULL;
		}
		p->p_fdtol = NULL;
		if (fdtol != NULL) {
			spin_unlock(&fdp->fd_spin);
			kfree(fdtol, M_FILEDESC_TO_LEADER);
			spin_lock(&fdp->fd_spin);
		}
	}
	if (--fdp->fd_refcnt > 0) {
		spin_unlock(&fdp->fd_spin);
		spin_lock(&p->p_spin);
		p->p_fd = repl;
		spin_unlock(&p->p_spin);
		return;
	}

	/*
	 * Even though we are the last reference to the structure allproc
	 * scans may still reference the structure.  Maintain proper
	 * locks until we can replace p->p_fd.
	 *
	 * Also note that kqueue's closef still needs to reference the
	 * fdp via p->p_fd, so we have to close the descriptors before
	 * we replace p->p_fd.
	 */
	for (i = 0; i <= fdp->fd_lastfile; ++i) {
		if (fdp->fd_files[i].fp) {
			fp = funsetfd_locked(fdp, i);
			if (fp) {
				spin_unlock(&fdp->fd_spin);
				if (SLIST_FIRST(&fp->f_klist))
					knote_fdclose(fp, fdp, i);
				closef(fp, p);
				spin_lock(&fdp->fd_spin);
			}
		}
	}
	spin_unlock(&fdp->fd_spin);

	/*
	 * Interlock against an allproc scan operations (typically frevoke).
	 */
	spin_lock(&p->p_spin);
	p->p_fd = repl;
	spin_unlock(&p->p_spin);

	/*
	 * Wait for any softrefs to go away.  This race rarely occurs so
	 * we can use a non-critical-path style poll/sleep loop.  The
	 * race only occurs against allproc scans.
	 *
	 * No new softrefs can occur with the fdp disconnected from the
	 * process.
	 */
	if (fdp->fd_softrefs) {
		kprintf("pid %d: Warning, fdp race avoided\n", p->p_pid);
		while (fdp->fd_softrefs)
			tsleep(&fdp->fd_softrefs, 0, "fdsoft", 1);
	}

	if (fdp->fd_files != fdp->fd_builtin_files)
		kfree(fdp->fd_files, M_FILEDESC);
	if (fdp->fd_cdir) {
		cache_drop(&fdp->fd_ncdir);
		vrele(fdp->fd_cdir);
	}
	if (fdp->fd_rdir) {
		cache_drop(&fdp->fd_nrdir);
		vrele(fdp->fd_rdir);
	}
	if (fdp->fd_jdir) {
		cache_drop(&fdp->fd_njdir);
		vrele(fdp->fd_jdir);
	}
	kfree(fdp, M_FILEDESC);
}

/*
 * Retrieve and reference the file pointer associated with a descriptor.
 *
 * MPSAFE
 */
struct file *
holdfp(struct filedesc *fdp, int fd, int flag)
{
	struct file* fp;

	spin_lock_shared(&fdp->fd_spin);
	if (((u_int)fd) >= fdp->fd_nfiles) {
		fp = NULL;
		goto done;
	}
	if ((fp = fdp->fd_files[fd].fp) == NULL)
		goto done;
	if ((fp->f_flag & flag) == 0 && flag != -1) {
		fp = NULL;
		goto done;
	}
	fhold(fp);
done:
	spin_unlock_shared(&fdp->fd_spin);
	return (fp);
}

/*
 * holdsock() - load the struct file pointer associated
 * with a socket into *fpp.  If an error occurs, non-zero
 * will be returned and *fpp will be set to NULL.
 *
 * MPSAFE
 */
int
holdsock(struct filedesc *fdp, int fd, struct file **fpp)
{
	struct file *fp;
	int error;

	spin_lock_shared(&fdp->fd_spin);
	if ((unsigned)fd >= fdp->fd_nfiles) {
		error = EBADF;
		fp = NULL;
		goto done;
	}
	if ((fp = fdp->fd_files[fd].fp) == NULL) {
		error = EBADF;
		goto done;
	}
	if (fp->f_type != DTYPE_SOCKET) {
		error = ENOTSOCK;
		goto done;
	}
	fhold(fp);
	error = 0;
done:
	spin_unlock_shared(&fdp->fd_spin);
	*fpp = fp;
	return (error);
}

/*
 * Convert a user file descriptor to a held file pointer.
 *
 * MPSAFE
 */
int
holdvnode(struct filedesc *fdp, int fd, struct file **fpp)
{
	struct file *fp;
	int error;

	spin_lock_shared(&fdp->fd_spin);
	if ((unsigned)fd >= fdp->fd_nfiles) {
		error = EBADF;
		fp = NULL;
		goto done;
	}
	if ((fp = fdp->fd_files[fd].fp) == NULL) {
		error = EBADF;
		goto done;
	}
	if (fp->f_type != DTYPE_VNODE && fp->f_type != DTYPE_FIFO) {
		fp = NULL;
		error = EINVAL;
		goto done;
	}
	fhold(fp);
	error = 0;
done:
	spin_unlock_shared(&fdp->fd_spin);
	*fpp = fp;
	return (error);
}

/*
 * For setugid programs, we don't want to people to use that setugidness
 * to generate error messages which write to a file which otherwise would
 * otherwise be off-limits to the process.
 *
 * This is a gross hack to plug the hole.  A better solution would involve
 * a special vop or other form of generalized access control mechanism.  We
 * go ahead and just reject all procfs file systems accesses as dangerous.
 *
 * Since setugidsafety calls this only for fd 0, 1 and 2, this check is
 * sufficient.  We also don't for check setugidness since we know we are.
 */
static int
is_unsafe(struct file *fp)
{
	if (fp->f_type == DTYPE_VNODE && 
	    ((struct vnode *)(fp->f_data))->v_tag == VT_PROCFS)
		return (1);
	return (0);
}

/*
 * Make this setguid thing safe, if at all possible.
 *
 * NOT MPSAFE - scans fdp without spinlocks, calls knote_fdclose()
 */
void
setugidsafety(struct proc *p)
{
	struct filedesc *fdp = p->p_fd;
	int i;

	/* Certain daemons might not have file descriptors. */
	if (fdp == NULL)
		return;

	/*
	 * note: fdp->fd_files may be reallocated out from under us while
	 * we are blocked in a close.  Be careful!
	 */
	for (i = 0; i <= fdp->fd_lastfile; i++) {
		if (i > 2)
			break;
		if (fdp->fd_files[i].fp && is_unsafe(fdp->fd_files[i].fp)) {
			struct file *fp;

			/*
			 * NULL-out descriptor prior to close to avoid
			 * a race while close blocks.
			 */
			if ((fp = funsetfd_locked(fdp, i)) != NULL) {
				knote_fdclose(fp, fdp, i);
				closef(fp, p);
			}
		}
	}
}

/*
 * Close any files on exec?
 *
 * NOT MPSAFE - scans fdp without spinlocks, calls knote_fdclose()
 */
void
fdcloseexec(struct proc *p)
{
	struct filedesc *fdp = p->p_fd;
	int i;

	/* Certain daemons might not have file descriptors. */
	if (fdp == NULL)
		return;

	/*
	 * We cannot cache fd_files since operations may block and rip
	 * them out from under us.
	 */
	for (i = 0; i <= fdp->fd_lastfile; i++) {
		if (fdp->fd_files[i].fp != NULL &&
		    (fdp->fd_files[i].fileflags & UF_EXCLOSE)) {
			struct file *fp;

			/*
			 * NULL-out descriptor prior to close to avoid
			 * a race while close blocks.
			 */
			if ((fp = funsetfd_locked(fdp, i)) != NULL) {
				knote_fdclose(fp, fdp, i);
				closef(fp, p);
			}
		}
	}
}

/*
 * It is unsafe for set[ug]id processes to be started with file
 * descriptors 0..2 closed, as these descriptors are given implicit
 * significance in the Standard C library.  fdcheckstd() will create a
 * descriptor referencing /dev/null for each of stdin, stdout, and
 * stderr that is not already open.
 *
 * NOT MPSAFE - calls falloc, vn_open, etc
 */
int
fdcheckstd(struct lwp *lp)
{
	struct nlookupdata nd;
	struct filedesc *fdp;
	struct file *fp;
	int retval;
	int i, error, flags, devnull;

	fdp = lp->lwp_proc->p_fd;
	if (fdp == NULL)
		return (0);
	devnull = -1;
	error = 0;
	for (i = 0; i < 3; i++) {
		if (fdp->fd_files[i].fp != NULL)
			continue;
		if (devnull < 0) {
			if ((error = falloc(lp, &fp, &devnull)) != 0)
				break;

			error = nlookup_init(&nd, "/dev/null", UIO_SYSSPACE,
						NLC_FOLLOW|NLC_LOCKVP);
			flags = FREAD | FWRITE;
			if (error == 0)
				error = vn_open(&nd, fp, flags, 0);
			if (error == 0)
				fsetfd(fdp, fp, devnull);
			else
				fsetfd(fdp, NULL, devnull);
			fdrop(fp);
			nlookup_done(&nd);
			if (error)
				break;
			KKASSERT(i == devnull);
		} else {
			error = kern_dup(DUP_FIXED, devnull, i, &retval);
			if (error != 0)
				break;
		}
	}
	return (error);
}

/*
 * Internal form of close.
 * Decrement reference count on file structure.
 * Note: td and/or p may be NULL when closing a file
 * that was being passed in a message.
 *
 * MPALMOSTSAFE - acquires mplock for VOP operations
 */
int
closef(struct file *fp, struct proc *p)
{
	struct vnode *vp;
	struct flock lf;
	struct filedesc_to_leader *fdtol;

	if (fp == NULL)
		return (0);

	/*
	 * POSIX record locking dictates that any close releases ALL
	 * locks owned by this process.  This is handled by setting
	 * a flag in the unlock to free ONLY locks obeying POSIX
	 * semantics, and not to free BSD-style file locks.
	 * If the descriptor was in a message, POSIX-style locks
	 * aren't passed with the descriptor.
	 */
	if (p != NULL && fp->f_type == DTYPE_VNODE &&
	    (((struct vnode *)fp->f_data)->v_flag & VMAYHAVELOCKS)
	) {
		if ((p->p_leader->p_flags & P_ADVLOCK) != 0) {
			lf.l_whence = SEEK_SET;
			lf.l_start = 0;
			lf.l_len = 0;
			lf.l_type = F_UNLCK;
			vp = (struct vnode *)fp->f_data;
			(void) VOP_ADVLOCK(vp, (caddr_t)p->p_leader, F_UNLCK,
					   &lf, F_POSIX);
		}
		fdtol = p->p_fdtol;
		if (fdtol != NULL) {
			lwkt_gettoken(&p->p_token);
			/*
			 * Handle special case where file descriptor table
			 * is shared between multiple process leaders.
			 */
			for (fdtol = fdtol->fdl_next;
			     fdtol != p->p_fdtol;
			     fdtol = fdtol->fdl_next) {
				if ((fdtol->fdl_leader->p_flags &
				     P_ADVLOCK) == 0)
					continue;
				fdtol->fdl_holdcount++;
				lf.l_whence = SEEK_SET;
				lf.l_start = 0;
				lf.l_len = 0;
				lf.l_type = F_UNLCK;
				vp = (struct vnode *)fp->f_data;
				(void) VOP_ADVLOCK(vp,
						   (caddr_t)fdtol->fdl_leader,
						   F_UNLCK, &lf, F_POSIX);
				fdtol->fdl_holdcount--;
				if (fdtol->fdl_holdcount == 0 &&
				    fdtol->fdl_wakeup != 0) {
					fdtol->fdl_wakeup = 0;
					wakeup(fdtol);
				}
			}
			lwkt_reltoken(&p->p_token);
		}
	}
	return (fdrop(fp));
}

/*
 * MPSAFE
 *
 * fhold() can only be called if f_count is already at least 1 (i.e. the
 * caller of fhold() already has a reference to the file pointer in some
 * manner or other). 
 *
 * f_count is not spin-locked.  Instead, atomic ops are used for
 * incrementing, decrementing, and handling the 1->0 transition.
 */
void
fhold(struct file *fp)
{
	atomic_add_int(&fp->f_count, 1);
}

/*
 * fdrop() - drop a reference to a descriptor
 *
 * MPALMOSTSAFE - acquires mplock for final close sequence
 */
int
fdrop(struct file *fp)
{
	struct flock lf;
	struct vnode *vp;
	int error;

	/*
	 * A combined fetch and subtract is needed to properly detect
	 * 1->0 transitions, otherwise two cpus dropping from a ref
	 * count of 2 might both try to run the 1->0 code.
	 */
	if (atomic_fetchadd_int(&fp->f_count, -1) > 1)
		return (0);

	KKASSERT(SLIST_FIRST(&fp->f_klist) == NULL);

	/*
	 * The last reference has gone away, we own the fp structure free
	 * and clear.
	 */
	if (fp->f_count < 0)
		panic("fdrop: count < 0");
	if ((fp->f_flag & FHASLOCK) && fp->f_type == DTYPE_VNODE &&
	    (((struct vnode *)fp->f_data)->v_flag & VMAYHAVELOCKS)
	) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		lf.l_type = F_UNLCK;
		vp = (struct vnode *)fp->f_data;
		(void) VOP_ADVLOCK(vp, (caddr_t)fp, F_UNLCK, &lf, 0);
	}
	if (fp->f_ops != &badfileops)
		error = fo_close(fp);
	else
		error = 0;
	ffree(fp);
	return (error);
}

/*
 * Apply an advisory lock on a file descriptor.
 *
 * Just attempt to get a record lock of the requested type on
 * the entire file (l_whence = SEEK_SET, l_start = 0, l_len = 0).
 *
 * MPALMOSTSAFE
 */
int
sys_flock(struct flock_args *uap)
{
	struct proc *p = curproc;
	struct file *fp;
	struct vnode *vp;
	struct flock lf;
	int error;

	if ((fp = holdfp(p->p_fd, uap->fd, -1)) == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_VNODE) {
		error = EOPNOTSUPP;
		goto done;
	}
	vp = (struct vnode *)fp->f_data;
	lf.l_whence = SEEK_SET;
	lf.l_start = 0;
	lf.l_len = 0;
	if (uap->how & LOCK_UN) {
		lf.l_type = F_UNLCK;
		fp->f_flag &= ~FHASLOCK;
		error = VOP_ADVLOCK(vp, (caddr_t)fp, F_UNLCK, &lf, 0);
		goto done;
	}
	if (uap->how & LOCK_EX)
		lf.l_type = F_WRLCK;
	else if (uap->how & LOCK_SH)
		lf.l_type = F_RDLCK;
	else {
		error = EBADF;
		goto done;
	}
	fp->f_flag |= FHASLOCK;
	if (uap->how & LOCK_NB)
		error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, 0);
	else
		error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, F_WAIT);
done:
	fdrop(fp);
	return (error);
}

/*
 * File Descriptor pseudo-device driver (/dev/fd/).
 *
 * Opening minor device N dup()s the file (if any) connected to file
 * descriptor N belonging to the calling process.  Note that this driver
 * consists of only the ``open()'' routine, because all subsequent
 * references to this file will be direct to the other driver.
 */
static int
fdopen(struct dev_open_args *ap)
{
	thread_t td = curthread;

	KKASSERT(td->td_lwp != NULL);

	/*
	 * XXX Kludge: set curlwp->lwp_dupfd to contain the value of the
	 * the file descriptor being sought for duplication. The error
	 * return ensures that the vnode for this device will be released
	 * by vn_open. Open will detect this special error and take the
	 * actions in dupfdopen below. Other callers of vn_open or VOP_OPEN
	 * will simply report the error.
	 */
	td->td_lwp->lwp_dupfd = minor(ap->a_head.a_dev);
	return (ENODEV);
}

/*
 * The caller has reserved the file descriptor dfd for us.  On success we
 * must fsetfd() it.  On failure the caller will clean it up.
 *
 * MPSAFE
 */
int
dupfdopen(struct filedesc *fdp, int dfd, int sfd, int mode, int error)
{
	struct file *wfp;
	struct file *xfp;
	int werror;

	if ((wfp = holdfp(fdp, sfd, -1)) == NULL)
		return (EBADF);

	/*
	 * Close a revoke/dup race.  Duping a descriptor marked as revoked
	 * will dup a dummy descriptor instead of the real one.
	 */
	if (wfp->f_flag & FREVOKED) {
		kprintf("Warning: attempt to dup() a revoked descriptor\n");
		fdrop(wfp);
		wfp = NULL;
		werror = falloc(NULL, &wfp, NULL);
		if (werror)
			return (werror);
	}

	/*
	 * There are two cases of interest here.
	 *
	 * For ENODEV simply dup sfd to file descriptor dfd and return.
	 *
	 * For ENXIO steal away the file structure from sfd and store it
	 * dfd.  sfd is effectively closed by this operation.
	 *
	 * Any other error code is just returned.
	 */
	switch (error) {
	case ENODEV:
		/*
		 * Check that the mode the file is being opened for is a
		 * subset of the mode of the existing descriptor.
		 */
		if (((mode & (FREAD|FWRITE)) | wfp->f_flag) != wfp->f_flag) {
			error = EACCES;
			break;
		}
		spin_lock(&fdp->fd_spin);
		fdp->fd_files[dfd].fileflags = fdp->fd_files[sfd].fileflags;
		fsetfd_locked(fdp, wfp, dfd);
		spin_unlock(&fdp->fd_spin);
		error = 0;
		break;
	case ENXIO:
		/*
		 * Steal away the file pointer from dfd, and stuff it into indx.
		 */
		spin_lock(&fdp->fd_spin);
		fdp->fd_files[dfd].fileflags = fdp->fd_files[sfd].fileflags;
		fsetfd(fdp, wfp, dfd);
		if ((xfp = funsetfd_locked(fdp, sfd)) != NULL) {
			spin_unlock(&fdp->fd_spin);
			fdrop(xfp);
		} else {
			spin_unlock(&fdp->fd_spin);
		}
		error = 0;
		break;
	default:
		break;
	}
	fdrop(wfp);
	return (error);
}

/*
 * NOT MPSAFE - I think these refer to a common file descriptor table
 * and we need to spinlock that to link fdtol in.
 */
struct filedesc_to_leader *
filedesc_to_leader_alloc(struct filedesc_to_leader *old,
			 struct proc *leader)
{
	struct filedesc_to_leader *fdtol;
	
	fdtol = kmalloc(sizeof(struct filedesc_to_leader), 
			M_FILEDESC_TO_LEADER, M_WAITOK | M_ZERO);
	fdtol->fdl_refcount = 1;
	fdtol->fdl_holdcount = 0;
	fdtol->fdl_wakeup = 0;
	fdtol->fdl_leader = leader;
	if (old != NULL) {
		fdtol->fdl_next = old->fdl_next;
		fdtol->fdl_prev = old;
		old->fdl_next = fdtol;
		fdtol->fdl_next->fdl_prev = fdtol;
	} else {
		fdtol->fdl_next = fdtol;
		fdtol->fdl_prev = fdtol;
	}
	return fdtol;
}

/*
 * Scan all file pointers in the system.  The callback is made with
 * the master list spinlock held exclusively.
 *
 * MPSAFE
 */
void
allfiles_scan_exclusive(int (*callback)(struct file *, void *), void *data)
{
	struct file *fp;
	int res;

	spin_lock(&filehead_spin);
	LIST_FOREACH(fp, &filehead, f_list) {
		res = callback(fp, data);
		if (res < 0)
			break;
	}
	spin_unlock(&filehead_spin);
}

/*
 * Get file structures.
 *
 * NOT MPSAFE - process list scan, SYSCTL_OUT (probably not mpsafe)
 */

struct sysctl_kern_file_info {
	int count;
	int error;
	struct sysctl_req *req;
};

static int sysctl_kern_file_callback(struct proc *p, void *data);

static int
sysctl_kern_file(SYSCTL_HANDLER_ARGS)
{
	struct sysctl_kern_file_info info;

	/*
	 * Note: because the number of file descriptors is calculated
	 * in different ways for sizing vs returning the data,
	 * there is information leakage from the first loop.  However,
	 * it is of a similar order of magnitude to the leakage from
	 * global system statistics such as kern.openfiles.
	 *
	 * When just doing a count, note that we cannot just count
	 * the elements and add f_count via the filehead list because 
	 * threaded processes share their descriptor table and f_count might
	 * still be '1' in that case.
	 *
	 * Since the SYSCTL op can block, we must hold the process to
	 * prevent it being ripped out from under us either in the 
	 * file descriptor loop or in the greater LIST_FOREACH.  The
	 * process may be in varying states of disrepair.  If the process
	 * is in SZOMB we may have caught it just as it is being removed
	 * from the allproc list, we must skip it in that case to maintain
	 * an unbroken chain through the allproc list.
	 */
	info.count = 0;
	info.error = 0;
	info.req = req;
	allproc_scan(sysctl_kern_file_callback, &info);

	/*
	 * When just calculating the size, overestimate a bit to try to
	 * prevent system activity from causing the buffer-fill call 
	 * to fail later on.
	 */
	if (req->oldptr == NULL) {
		info.count = (info.count + 16) + (info.count / 10);
		info.error = SYSCTL_OUT(req, NULL,
					info.count * sizeof(struct kinfo_file));
	}
	return (info.error);
}

static int
sysctl_kern_file_callback(struct proc *p, void *data)
{
	struct sysctl_kern_file_info *info = data;
	struct kinfo_file kf;
	struct filedesc *fdp;
	struct file *fp;
	uid_t uid;
	int n;

	if (p->p_stat == SIDL || p->p_stat == SZOMB)
		return(0);
	if (!PRISON_CHECK(info->req->td->td_ucred, p->p_ucred) != 0)
		return(0);

	/*
	 * Softref the fdp to prevent it from being destroyed
	 */
	spin_lock(&p->p_spin);
	if ((fdp = p->p_fd) == NULL) {
		spin_unlock(&p->p_spin);
		return(0);
	}
	atomic_add_int(&fdp->fd_softrefs, 1);
	spin_unlock(&p->p_spin);

	/*
	 * The fdp's own spinlock prevents the contents from being
	 * modified.
	 */
	spin_lock_shared(&fdp->fd_spin);
	for (n = 0; n < fdp->fd_nfiles; ++n) {
		if ((fp = fdp->fd_files[n].fp) == NULL)
			continue;
		if (info->req->oldptr == NULL) {
			++info->count;
		} else {
			uid = p->p_ucred ? p->p_ucred->cr_uid : -1;
			kcore_make_file(&kf, fp, p->p_pid, uid, n);
			spin_unlock_shared(&fdp->fd_spin);
			info->error = SYSCTL_OUT(info->req, &kf, sizeof(kf));
			spin_lock_shared(&fdp->fd_spin);
			if (info->error)
				break;
		}
	}
	spin_unlock_shared(&fdp->fd_spin);
	atomic_subtract_int(&fdp->fd_softrefs, 1);
	if (info->error)
		return(-1);
	return(0);
}

SYSCTL_PROC(_kern, KERN_FILE, file, CTLTYPE_OPAQUE|CTLFLAG_RD,
    0, 0, sysctl_kern_file, "S,file", "Entire file table");

SYSCTL_INT(_kern, OID_AUTO, minfilesperproc, CTLFLAG_RW,
    &minfilesperproc, 0, "Minimum files allowed open per process");
SYSCTL_INT(_kern, KERN_MAXFILESPERPROC, maxfilesperproc, CTLFLAG_RW, 
    &maxfilesperproc, 0, "Maximum files allowed open per process");
SYSCTL_INT(_kern, OID_AUTO, maxfilesperuser, CTLFLAG_RW,
    &maxfilesperuser, 0, "Maximum files allowed open per user");

SYSCTL_INT(_kern, KERN_MAXFILES, maxfiles, CTLFLAG_RW, 
    &maxfiles, 0, "Maximum number of files");

SYSCTL_INT(_kern, OID_AUTO, maxfilesrootres, CTLFLAG_RW, 
    &maxfilesrootres, 0, "Descriptors reserved for root use");

SYSCTL_INT(_kern, OID_AUTO, openfiles, CTLFLAG_RD, 
	&nfiles, 0, "System-wide number of open files");

static void
fildesc_drvinit(void *unused)
{
	int fd;

	for (fd = 0; fd < NUMFDESC; fd++) {
		make_dev(&fildesc_ops, fd,
			 UID_BIN, GID_BIN, 0666, "fd/%d", fd);
	}

	make_dev(&fildesc_ops, 0, UID_ROOT, GID_WHEEL, 0666, "stdin");
	make_dev(&fildesc_ops, 1, UID_ROOT, GID_WHEEL, 0666, "stdout");
	make_dev(&fildesc_ops, 2, UID_ROOT, GID_WHEEL, 0666, "stderr");
}

/*
 * MPSAFE
 */
struct fileops badfileops = {
	.fo_read = badfo_readwrite,
	.fo_write = badfo_readwrite,
	.fo_ioctl = badfo_ioctl,
	.fo_kqfilter = badfo_kqfilter,
	.fo_stat = badfo_stat,
	.fo_close = badfo_close,
	.fo_shutdown = badfo_shutdown
};

int
badfo_readwrite(
	struct file *fp,
	struct uio *uio,
	struct ucred *cred,
	int flags
) {
	return (EBADF);
}

int
badfo_ioctl(struct file *fp, u_long com, caddr_t data,
	    struct ucred *cred, struct sysmsg *msgv)
{
	return (EBADF);
}

/*
 * Must return an error to prevent registration, typically
 * due to a revoked descriptor (file_filtops assigned).
 */
int
badfo_kqfilter(struct file *fp, struct knote *kn)
{
	return (EOPNOTSUPP);
}

/*
 * MPSAFE
 */
int
badfo_stat(struct file *fp, struct stat *sb, struct ucred *cred)
{
	return (EBADF);
}

/*
 * MPSAFE
 */
int
badfo_close(struct file *fp)
{
	return (EBADF);
}

/*
 * MPSAFE
 */
int
badfo_shutdown(struct file *fp, int how)
{
	return (EBADF);
}

/*
 * MPSAFE
 */
int
nofo_shutdown(struct file *fp, int how)
{
	return (EOPNOTSUPP);
}

SYSINIT(fildescdev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR,
					fildesc_drvinit,NULL)
