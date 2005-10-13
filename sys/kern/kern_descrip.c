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
 *	@(#)kern_descrip.c	8.6 (Berkeley) 4/19/94
 * $FreeBSD: src/sys/kern/kern_descrip.c,v 1.81.2.19 2004/02/28 00:43:31 tegge Exp $
 * $DragonFly: src/sys/kern/kern_descrip.c,v 1.49 2005/10/13 00:06:28 dillon Exp $
 */

#include "opt_compat.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/conf.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/unistd.h>
#include <sys/resourcevar.h>
#include <sys/event.h>
#include <sys/kern_syscall.h>
#include <sys/kcore.h>
#include <sys/kinfo.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <sys/thread2.h>
#include <sys/file2.h>

static MALLOC_DEFINE(M_FILEDESC, "file desc", "Open file descriptor table");
static MALLOC_DEFINE(M_FILEDESC_TO_LEADER, "file desc to leader",
		     "file desc to leader structures");
MALLOC_DEFINE(M_FILE, "file", "Open file structure");
static MALLOC_DEFINE(M_SIGIO, "sigio", "sigio structures");

static	 d_open_t  fdopen;
#define NUMFDESC 64

#define CDEV_MAJOR 22
static struct cdevsw fildesc_cdevsw = {
	/* name */	"FD",
	/* maj */	CDEV_MAJOR,
	/* flags */	0,
	/* port */      NULL,
	/* clone */	NULL,

	/* open */	fdopen,
	/* close */	noclose,
	/* read */	noread,
	/* write */	nowrite,
	/* ioctl */	noioctl,
	/* poll */	nopoll,
	/* mmap */	nommap,
	/* strategy */	nostrategy,
	/* dump */	nodump,
	/* psize */	nopsize
};

static int badfo_readwrite (struct file *fp, struct uio *uio,
    struct ucred *cred, int flags, struct thread *td);
static int badfo_ioctl (struct file *fp, u_long com, caddr_t data,
    struct thread *td);
static int badfo_poll (struct file *fp, int events,
    struct ucred *cred, struct thread *td);
static int badfo_kqfilter (struct file *fp, struct knote *kn);
static int badfo_stat (struct file *fp, struct stat *sb, struct thread *td);
static int badfo_close (struct file *fp, struct thread *td);
static int badfo_shutdown (struct file *fp, int how, struct thread *td);

/*
 * Descriptor management.
 */
struct filelist filehead;	/* head of list of open files */
int nfiles;			/* actual number of open files */
extern int cmask;	

/*
 * System calls on descriptors.
 */
/* ARGSUSED */
int
getdtablesize(struct getdtablesize_args *uap) 
{
	struct proc *p = curproc;

	uap->sysmsg_result = 
	    min((int)p->p_rlimit[RLIMIT_NOFILE].rlim_cur, maxfilesperproc);
	return (0);
}

/*
 * Duplicate a file descriptor to a particular value.
 *
 * note: keep in mind that a potential race condition exists when closing
 * descriptors from a shared descriptor table (via rfork).
 */
/* ARGSUSED */
int
dup2(struct dup2_args *uap)
{
	int error;

	error = kern_dup(DUP_FIXED, uap->from, uap->to, uap->sysmsg_fds);

	return (error);
}

/*
 * Duplicate a file descriptor.
 */
/* ARGSUSED */
int
dup(struct dup_args *uap)
{
	int error;

	error = kern_dup(DUP_VARIABLE, uap->fd, 0, uap->sysmsg_fds);

	return (error);
}

int
kern_fcntl(int fd, int cmd, union fcntl_dat *dat)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	char *pop;
	struct vnode *vp;
	u_int newmin;
	int tmp, error, flg = F_POSIX;

	KKASSERT(p);

	if ((unsigned)fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_files[fd].fp) == NULL)
		return (EBADF);
	pop = &fdp->fd_files[fd].fileflags;

	switch (cmd) {
	case F_DUPFD:
		newmin = dat->fc_fd;
		if (newmin >= p->p_rlimit[RLIMIT_NOFILE].rlim_cur ||
		    newmin > maxfilesperproc)
			return (EINVAL);
		error = kern_dup(DUP_VARIABLE, fd, newmin, &dat->fc_fd);
		return (error);

	case F_GETFD:
		dat->fc_cloexec = (*pop & UF_EXCLOSE) ? FD_CLOEXEC : 0;
		return (0);

	case F_SETFD:
		*pop = (*pop &~ UF_EXCLOSE) |
		    (dat->fc_cloexec & FD_CLOEXEC ? UF_EXCLOSE : 0);
		return (0);

	case F_GETFL:
		dat->fc_flags = OFLAGS(fp->f_flag);
		return (0);

	case F_SETFL:
		fhold(fp);
		fp->f_flag &= ~FCNTLFLAGS;
		fp->f_flag |= FFLAGS(dat->fc_flags & ~O_ACCMODE) & FCNTLFLAGS;
		tmp = fp->f_flag & FNONBLOCK;
		error = fo_ioctl(fp, FIONBIO, (caddr_t)&tmp, td);
		if (error) {
			fdrop(fp, td);
			return (error);
		}
		tmp = fp->f_flag & FASYNC;
		error = fo_ioctl(fp, FIOASYNC, (caddr_t)&tmp, td);
		if (!error) {
			fdrop(fp, td);
			return (0);
		}
		fp->f_flag &= ~FNONBLOCK;
		tmp = 0;
		fo_ioctl(fp, FIONBIO, (caddr_t)&tmp, td);
		fdrop(fp, td);
		return (error);

	case F_GETOWN:
		fhold(fp);
		error = fo_ioctl(fp, FIOGETOWN, (caddr_t)&dat->fc_owner, td);
		fdrop(fp, td);
		return(error);

	case F_SETOWN:
		fhold(fp);
		error = fo_ioctl(fp, FIOSETOWN, (caddr_t)&dat->fc_owner, td);
		fdrop(fp, td);
		return(error);

	case F_SETLKW:
		flg |= F_WAIT;
		/* Fall into F_SETLK */

	case F_SETLK:
		if (fp->f_type != DTYPE_VNODE)
			return (EBADF);
		vp = (struct vnode *)fp->f_data;

		/*
		 * copyin/lockop may block
		 */
		fhold(fp);
		if (dat->fc_flock.l_whence == SEEK_CUR)
			dat->fc_flock.l_start += fp->f_offset;

		switch (dat->fc_flock.l_type) {
		case F_RDLCK:
			if ((fp->f_flag & FREAD) == 0) {
				error = EBADF;
				break;
			}
			p->p_leader->p_flag |= P_ADVLOCK;
			error = VOP_ADVLOCK(vp, (caddr_t)p->p_leader, F_SETLK,
			    &dat->fc_flock, flg);
			break;
		case F_WRLCK:
			if ((fp->f_flag & FWRITE) == 0) {
				error = EBADF;
				break;
			}
			p->p_leader->p_flag |= P_ADVLOCK;
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
		/* Check for race with close */
		if ((unsigned) fd >= fdp->fd_nfiles ||
		    fp != fdp->fd_files[fd].fp) {
			dat->fc_flock.l_whence = SEEK_SET;
			dat->fc_flock.l_start = 0;
			dat->fc_flock.l_len = 0;
			dat->fc_flock.l_type = F_UNLCK;
			(void) VOP_ADVLOCK(vp, (caddr_t)p->p_leader,
					   F_UNLCK, &dat->fc_flock, F_POSIX);
		}
		fdrop(fp, td);
		return(error);

	case F_GETLK:
		if (fp->f_type != DTYPE_VNODE)
			return (EBADF);
		vp = (struct vnode *)fp->f_data;
		/*
		 * copyin/lockop may block
		 */
		fhold(fp);
		if (dat->fc_flock.l_type != F_RDLCK &&
		    dat->fc_flock.l_type != F_WRLCK &&
		    dat->fc_flock.l_type != F_UNLCK) {
			fdrop(fp, td);
			return (EINVAL);
		}
		if (dat->fc_flock.l_whence == SEEK_CUR)
			dat->fc_flock.l_start += fp->f_offset;
		error = VOP_ADVLOCK(vp, (caddr_t)p->p_leader, F_GETLK,
			    &dat->fc_flock, F_POSIX);
		fdrop(fp, td);
		return(error);
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}

/*
 * The file control system call.
 */
int
fcntl(struct fcntl_args *uap)
{
	union fcntl_dat dat;
	int error;

	switch (uap->cmd) {
	case F_DUPFD:
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

	error = kern_fcntl(uap->fd, uap->cmd, &dat);

	if (error == 0) {
		switch (uap->cmd) {
		case F_DUPFD:
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
 * The type flag can be either DUP_FIXED or DUP_VARIABLE.  DUP_FIXED tells
 * kern_dup() to destructively dup over an existing file descriptor if new
 * is already open.  DUP_VARIABLE tells kern_dup() to find the lowest
 * unused file descriptor that is greater than or equal to new.
 */
int
kern_dup(enum dup_type type, int old, int new, int *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct file *delfp;
	int holdleaders;
	boolean_t fdalloced = FALSE;
	int error, newfd;

	/*
	 * Verify that we have a valid descriptor to dup from and
	 * possibly to dup to.
	 */
	if (old < 0 || new < 0 || new > p->p_rlimit[RLIMIT_NOFILE].rlim_cur ||
	    new >= maxfilesperproc)
		return (EBADF);
	if (old >= fdp->fd_nfiles || fdp->fd_files[old].fp == NULL)
		return (EBADF);
	if (type == DUP_FIXED && old == new) {
		*res = new;
		return (0);
	}
	fp = fdp->fd_files[old].fp;
	fhold(fp);

	/*
	 * Expand the table for the new descriptor if needed.  This may
	 * block and drop and reacquire the fidedesc lock.
	 */
	if (type == DUP_VARIABLE || new >= fdp->fd_nfiles) {
		error = fdalloc(p, new, &newfd);
		if (error) {
			fdrop(fp, td);
			return (error);
		}
		fdalloced = TRUE;
	}
	if (type == DUP_VARIABLE)
		new = newfd;

	/*
	 * If the old file changed out from under us then treat it as a
	 * bad file descriptor.  Userland should do its own locking to
	 * avoid this case.
	 */
	if (fdp->fd_files[old].fp != fp) {
		if (fdp->fd_files[new].fp == NULL) {
			if (fdalloced)
				fdreserve(fdp, newfd, -1);
			if (new < fdp->fd_freefile)
				fdp->fd_freefile = new;
			while (fdp->fd_lastfile > 0 &&
			    fdp->fd_files[fdp->fd_lastfile].fp == NULL)
				fdp->fd_lastfile--;
		}
		fdrop(fp, td);
		return (EBADF);
	}
	KASSERT(old != new, ("new fd is same as old"));

	/*
	 * Save info on the descriptor being overwritten.  We have
	 * to do the unmap now, but we cannot close it without
	 * introducing an ownership race for the slot.
	 */
	delfp = fdp->fd_files[new].fp;
	if (delfp != NULL && p->p_fdtol != NULL) {
		/*
		 * Ask fdfree() to sleep to ensure that all relevant
		 * process leaders can be traversed in closef().
		 */
		fdp->fd_holdleaderscount++;
		holdleaders = 1;
	} else
		holdleaders = 0;
	KASSERT(delfp == NULL || type == DUP_FIXED,
	    ("dup() picked an open file"));
#if 0
	if (delfp && (fdp->fd_files[new].fileflags & UF_MAPPED))
		(void) munmapfd(p, new);
#endif

	/*
	 * Duplicate the source descriptor, update lastfile
	 */
	if (new > fdp->fd_lastfile)
		fdp->fd_lastfile = new;
	if (!fdalloced && fdp->fd_files[new].fp == NULL)
		fdreserve(fdp, new, 1);
	fdp->fd_files[new].fp = fp;
	fdp->fd_files[new].fileflags = 
			fdp->fd_files[old].fileflags & ~UF_EXCLOSE;
	*res = new;

	/*
	 * If we dup'd over a valid file, we now own the reference to it
	 * and must dispose of it using closef() semantics (as if a
	 * close() were performed on it).
	 */
	if (delfp) {
		(void) closef(delfp, td);
		if (holdleaders) {
			fdp->fd_holdleaderscount--;
			if (fdp->fd_holdleaderscount == 0 &&
			    fdp->fd_holdleaderswakeup != 0) {
				fdp->fd_holdleaderswakeup = 0;
				wakeup(&fdp->fd_holdleaderscount);
			}
		}
	}
	return (0);
}

/*
 * If sigio is on the list associated with a process or process group,
 * disable signalling from the device, remove sigio from the list and
 * free sigio.
 */
void
funsetown(struct sigio *sigio)
{
	if (sigio == NULL)
		return;
	crit_enter();
	*(sigio->sio_myref) = NULL;
	crit_exit();
	if (sigio->sio_pgid < 0) {
		SLIST_REMOVE(&sigio->sio_pgrp->pg_sigiolst, sigio,
			     sigio, sio_pgsigio);
	} else /* if ((*sigiop)->sio_pgid > 0) */ {
		SLIST_REMOVE(&sigio->sio_proc->p_sigiolst, sigio,
			     sigio, sio_pgsigio);
	}
	crfree(sigio->sio_ucred);
	free(sigio, M_SIGIO);
}

/* Free a list of sigio structures. */
void
funsetownlst(struct sigiolst *sigiolst)
{
	struct sigio *sigio;

	while ((sigio = SLIST_FIRST(sigiolst)) != NULL)
		funsetown(sigio);
}

/*
 * This is common code for FIOSETOWN ioctl called by fcntl(fd, F_SETOWN, arg).
 *
 * After permission checking, add a sigio structure to the sigio list for
 * the process or process group.
 */
int
fsetown(pid_t pgid, struct sigio **sigiop)
{
	struct proc *proc;
	struct pgrp *pgrp;
	struct sigio *sigio;

	if (pgid == 0) {
		funsetown(*sigiop);
		return (0);
	}
	if (pgid > 0) {
		proc = pfind(pgid);
		if (proc == NULL)
			return (ESRCH);

		/*
		 * Policy - Don't allow a process to FSETOWN a process
		 * in another session.
		 *
		 * Remove this test to allow maximum flexibility or
		 * restrict FSETOWN to the current process or process
		 * group for maximum safety.
		 */
		if (proc->p_session != curproc->p_session)
			return (EPERM);

		pgrp = NULL;
	} else /* if (pgid < 0) */ {
		pgrp = pgfind(-pgid);
		if (pgrp == NULL)
			return (ESRCH);

		/*
		 * Policy - Don't allow a process to FSETOWN a process
		 * in another session.
		 *
		 * Remove this test to allow maximum flexibility or
		 * restrict FSETOWN to the current process or process
		 * group for maximum safety.
		 */
		if (pgrp->pg_session != curproc->p_session)
			return (EPERM);

		proc = NULL;
	}
	funsetown(*sigiop);
	sigio = malloc(sizeof(struct sigio), M_SIGIO, M_WAITOK);
	if (pgid > 0) {
		SLIST_INSERT_HEAD(&proc->p_sigiolst, sigio, sio_pgsigio);
		sigio->sio_proc = proc;
	} else {
		SLIST_INSERT_HEAD(&pgrp->pg_sigiolst, sigio, sio_pgsigio);
		sigio->sio_pgrp = pgrp;
	}
	sigio->sio_pgid = pgid;
	sigio->sio_ucred = crhold(curproc->p_ucred);
	/* It would be convenient if p_ruid was in ucred. */
	sigio->sio_ruid = curproc->p_ucred->cr_ruid;
	sigio->sio_myref = sigiop;
	crit_enter();
	*sigiop = sigio;
	crit_exit();
	return (0);
}

/*
 * This is common code for FIOGETOWN ioctl called by fcntl(fd, F_GETOWN, arg).
 */
pid_t
fgetown(struct sigio *sigio)
{
	return (sigio != NULL ? sigio->sio_pgid : 0);
}

/*
 * Close many file descriptors.
 */
/* ARGSUSED */

int
closefrom(struct closefrom_args *uap)
{
	return(kern_closefrom(uap->fd));
}

int
kern_closefrom(int fd)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp;

	KKASSERT(p);
	fdp = p->p_fd;

	if (fd < 0 || fd > fdp->fd_lastfile)
		return (0);

	do {
		if (kern_close(fdp->fd_lastfile) == EINTR)
			return (EINTR);
	} while (fdp->fd_lastfile > fd);

	return (0);
}

/*
 * Close a file descriptor.
 */
/* ARGSUSED */

int
close(struct close_args *uap)
{
	return(kern_close(uap->fd));
}

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

	if ((unsigned)fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_files[fd].fp) == NULL)
		return (EBADF);
#if 0
	if (fdp->fd_files[fd].fileflags & UF_MAPPED)
		(void) munmapfd(p, fd);
#endif
	funsetfd(fdp, fd);
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
	while (fdp->fd_lastfile > 0 && fdp->fd_files[fdp->fd_lastfile].fp == NULL)
		fdp->fd_lastfile--;
	if (fd < fdp->fd_knlistsize)
		knote_fdclose(p, fd);
	error = closef(fp, td);
	if (holdleaders) {
		fdp->fd_holdleaderscount--;
		if (fdp->fd_holdleaderscount == 0 &&
		    fdp->fd_holdleaderswakeup != 0) {
			fdp->fd_holdleaderswakeup = 0;
			wakeup(&fdp->fd_holdleaderscount);
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
	struct filedesc *fdp;
	struct file *fp;
	int error;

	KKASSERT(p);

	fdp = p->p_fd;
	if ((unsigned)fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_files[fd].fp) == NULL)
		return (EBADF);
	fhold(fp);
	error = fo_shutdown(fp, how, td);
	fdrop(fp, td);

	return (error);
}

int
shutdown(struct shutdown_args *uap)
{
	int error;

	error = kern_shutdown(uap->s, uap->how);

	return (error);
}

int
kern_fstat(int fd, struct stat *ub)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp;
	struct file *fp;
	int error;

	KKASSERT(p);

	fdp = p->p_fd;
	if ((unsigned)fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_files[fd].fp) == NULL)
		return (EBADF);
	fhold(fp);
	error = fo_stat(fp, ub, td);
	fdrop(fp, td);

	return (error);
}

/*
 * Return status information about a file descriptor.
 */
int
fstat(struct fstat_args *uap)
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
 */
/* ARGSUSED */
int
fpathconf(struct fpathconf_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp;
	struct file *fp;
	struct vnode *vp;
	int error = 0;

	KKASSERT(p);
	fdp = p->p_fd;
	if ((unsigned)uap->fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_files[uap->fd].fp) == NULL)
		return (EBADF);

	fhold(fp);

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
		error = VOP_PATHCONF(vp, uap->name, uap->sysmsg_fds);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	fdrop(fp, td);
	return(error);
}

static int fdexpand;
SYSCTL_INT(_debug, OID_AUTO, fdexpand, CTLFLAG_RD, &fdexpand, 0, "");

static void
fdgrow(struct filedesc *fdp, int want)
{
	struct fdnode *newfiles;
	struct fdnode *oldfiles;
	int nf, extra;

	nf = fdp->fd_nfiles;
	do {
		/* nf has to be of the form 2^n - 1 */
		nf = 2 * nf + 1;
	} while (nf <= want);

	newfiles = malloc(nf * sizeof(struct fdnode), M_FILEDESC, M_WAITOK);

	/*
	 * deal with file-table extend race that might have occured
	 * when malloc was blocked.
	 */
	if (fdp->fd_nfiles >= nf) {
		free(newfiles, M_FILEDESC);
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

	if (oldfiles != fdp->fd_builtin_files)
		free(oldfiles, M_FILEDESC);
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

void
fdreserve(struct filedesc *fdp, int fd, int incr)
{
	while (fd >= 0) {
		fdp->fd_files[fd].allocated += incr;
		KKASSERT(fdp->fd_files[fd].allocated >= 0);
		fd = left_ancestor(fd);
	}
}

/*
 * Allocate a file descriptor for the process.
 */
int
fdalloc(struct proc *p, int want, int *result)
{
	struct filedesc *fdp = p->p_fd;
	int fd, rsize, rsum, node, lim;

	lim = min((int)p->p_rlimit[RLIMIT_NOFILE].rlim_cur, maxfilesperproc);
	if (want >= lim)
		return (EMFILE);
	if (want >= fdp->fd_nfiles)
		fdgrow(fdp, want);

	/*
	 * Search for a free descriptor starting at the higher
	 * of want or fd_freefile.  If that fails, consider
	 * expanding the ofile array.
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
	if (fdp->fd_nfiles >= lim)
		return (EMFILE);
	fdgrow(fdp, want);
	goto retry;

found:
	KKASSERT(fd < fdp->fd_nfiles);
	fdp->fd_files[fd].fileflags = 0;
	if (fd > fdp->fd_lastfile)
		fdp->fd_lastfile = fd;
	if (want <= fdp->fd_freefile)
		fdp->fd_freefile = fd;
	*result = fd;
	KKASSERT(fdp->fd_files[fd].fp == NULL);
	fdreserve(fdp, fd, 1);
	return (0);
}

/*
 * Check to see whether n user file descriptors
 * are available to the process p.
 */
int
fdavail(struct proc *p, int n)
{
	struct filedesc *fdp = p->p_fd;
	struct fdnode *fdnode;
	int i, lim, last;

	lim = min((int)p->p_rlimit[RLIMIT_NOFILE].rlim_cur, maxfilesperproc);
	if ((i = lim - fdp->fd_nfiles) > 0 && (n -= i) <= 0)
		return (1);

	last = min(fdp->fd_nfiles, lim);
	fdnode = &fdp->fd_files[fdp->fd_freefile];
	for (i = last - fdp->fd_freefile; --i >= 0; ++fdnode) {
		if (fdnode->fp == NULL && --n <= 0)
			return (1);
	}
	return (0);
}

/*
 * falloc:
 *	Create a new open file structure and allocate a file decriptor
 *	for the process that refers to it.  If p is NULL, no descriptor
 *	is allocated and the file pointer is returned unassociated with
 *	any process.  resultfd is only used if p is not NULL and may
 *	separately be NULL indicating that you don't need the returned fd.
 *
 *	A held file pointer is returned.  If a descriptor has been allocated
 *	an additional hold on the fp will be made due to the fd_files[]
 *	reference.
 */
int
falloc(struct proc *p, struct file **resultfp, int *resultfd)
{
	static struct timeval lastfail;
	static int curfail;
	struct file *fp;
	int error;

	fp = NULL;

	/*
	 * Handle filetable full issues and root overfill.
	 */
	if (nfiles >= maxfiles - maxfilesrootres &&
	    ((p && p->p_ucred->cr_ruid != 0) || nfiles >= maxfiles)) {
		if (ppsratecheck(&lastfail, &curfail, 1)) {
			printf("kern.maxfiles limit exceeded by uid %d, please see tuning(7).\n",
				(p ? p->p_ucred->cr_ruid : -1));
		}
		error = ENFILE;
		goto done;
	}

	/*
	 * Allocate a new file descriptor.
	 */
	nfiles++;
	fp = malloc(sizeof(struct file), M_FILE, M_WAITOK | M_ZERO);
	fp->f_count = 1;
	fp->f_ops = &badfileops;
	fp->f_seqcount = 1;
	if (p)
		fp->f_cred = crhold(p->p_ucred);
	else
		fp->f_cred = crhold(proc0.p_ucred);
	LIST_INSERT_HEAD(&filehead, fp, f_list);
	if (resultfd) {
		if ((error = fsetfd(p, fp, resultfd)) != 0) {
			fdrop(fp, p->p_thread);
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
 * Associate a file pointer with a file descriptor.  On success the fp
 * will have an additional ref representing the fd_files[] association.
 */
int
fsetfd(struct proc *p, struct file *fp, int *resultfd)
{
	int fd, error;

	fd = -1;
	if ((error = fdalloc(p, 0, &fd)) == 0) {
		fhold(fp);
		p->p_fd->fd_files[fd].fp = fp;
	}
	*resultfd = fd;
	return (error);
}

void
funsetfd(struct filedesc *fdp, int fd)
{
	fdp->fd_files[fd].fp = NULL;
	fdp->fd_files[fd].fileflags = 0;
	fdreserve(fdp, fd, -1);
	if (fd < fdp->fd_freefile)
		fdp->fd_freefile = fd;
}

void
fsetcred(struct file *fp, struct ucred *cr)
{
	crhold(cr);
	crfree(fp->f_cred);
	fp->f_cred = cr;
}

/*
 * Free a file descriptor.
 */
void
ffree(struct file *fp)
{
	KASSERT((fp->f_count == 0), ("ffree: fp_fcount not 0!"));
	LIST_REMOVE(fp, f_list);
	crfree(fp->f_cred);
	if (fp->f_ncp) {
	    cache_drop(fp->f_ncp);
	    fp->f_ncp = NULL;
	}
	nfiles--;
	free(fp, M_FILE);
}

/*
 * Build a new filedesc structure.
 */
struct filedesc *
fdinit(struct proc *p)
{
	struct filedesc *newfdp;
	struct filedesc *fdp = p->p_fd;

	newfdp = malloc(sizeof(struct filedesc), M_FILEDESC, M_WAITOK|M_ZERO);
	if (fdp->fd_cdir) {
		newfdp->fd_cdir = fdp->fd_cdir;
		vref(newfdp->fd_cdir);
		newfdp->fd_ncdir = cache_hold(fdp->fd_ncdir);
	}

	/*
	 * rdir may not be set in e.g. proc0 or anything vm_fork'd off of
	 * proc0, but should unconditionally exist in other processes.
	 */
	if (fdp->fd_rdir) {
		newfdp->fd_rdir = fdp->fd_rdir;
		vref(newfdp->fd_rdir);
		newfdp->fd_nrdir = cache_hold(fdp->fd_nrdir);
	}
	if (fdp->fd_jdir) {
		newfdp->fd_jdir = fdp->fd_jdir;
		vref(newfdp->fd_jdir);
		newfdp->fd_njdir = cache_hold(fdp->fd_njdir);
	}

	/* Create the file descriptor table. */
	newfdp->fd_refcnt = 1;
	newfdp->fd_cmask = cmask;
	newfdp->fd_files = newfdp->fd_builtin_files;
	newfdp->fd_nfiles = NDFILE;
	newfdp->fd_knlistsize = -1;

	return (newfdp);
}

/*
 * Share a filedesc structure.
 */
struct filedesc *
fdshare(struct proc *p)
{
	p->p_fd->fd_refcnt++;
	return (p->p_fd);
}

/*
 * Copy a filedesc structure.
 */
struct filedesc *
fdcopy(struct proc *p)
{
	struct filedesc *newfdp, *fdp = p->p_fd;
	struct fdnode *fdnode;
	int i;

	/* Certain daemons might not have file descriptors. */
	if (fdp == NULL)
		return (NULL);

	newfdp = malloc(sizeof(struct filedesc), M_FILEDESC, M_WAITOK);
	*newfdp = *fdp;
	if (newfdp->fd_cdir) {
		vref(newfdp->fd_cdir);
		newfdp->fd_ncdir = cache_hold(newfdp->fd_ncdir);
	}
	/*
	 * We must check for fd_rdir here, at least for now because
	 * the init process is created before we have access to the
	 * rootvode to take a reference to it.
	 */
	if (newfdp->fd_rdir) {
		vref(newfdp->fd_rdir);
		newfdp->fd_nrdir = cache_hold(newfdp->fd_nrdir);
	}
	if (newfdp->fd_jdir) {
		vref(newfdp->fd_jdir);
		newfdp->fd_njdir = cache_hold(newfdp->fd_njdir);
	}
	newfdp->fd_refcnt = 1;

	/*
	 * If the number of open files fits in the internal arrays
	 * of the open file structure, use them, otherwise allocate
	 * additional memory for the number of descriptors currently
	 * in use.
	 */
	if (newfdp->fd_lastfile < NDFILE) {
		newfdp->fd_files = newfdp->fd_builtin_files;
		i = NDFILE;
	} else {
		/*
		 * Compute the smallest file table size
		 * for the file descriptors currently in use,
		 * allowing the table to shrink.
		 */
		i = newfdp->fd_nfiles;
		while ((i-1)/2 > newfdp->fd_lastfile && (i-1)/2 > NDFILE)
			i = (i-1)/2;
		newfdp->fd_files = malloc(i * sizeof(struct fdnode),
					  M_FILEDESC, M_WAITOK);
	}
	newfdp->fd_nfiles = i;

	if (fdp->fd_files != fdp->fd_builtin_files ||
	    newfdp->fd_files != newfdp->fd_builtin_files
	) {
		bcopy(fdp->fd_files, newfdp->fd_files, 
		      i * sizeof(struct fdnode));
	}

	/*
	 * kq descriptors cannot be copied.
	 */
	if (newfdp->fd_knlistsize != -1) {
		fdnode = &newfdp->fd_files[newfdp->fd_lastfile];
		for (i = newfdp->fd_lastfile; i >= 0; i--, fdnode--) {
			if (fdnode->fp != NULL && fdnode->fp->f_type == DTYPE_KQUEUE)
				funsetfd(newfdp, i);	/* nulls out *fpp */
			if (fdnode->fp == NULL && i == newfdp->fd_lastfile && i > 0)
				newfdp->fd_lastfile--;
		}
		newfdp->fd_knlist = NULL;
		newfdp->fd_knlistsize = -1;
		newfdp->fd_knhash = NULL;
		newfdp->fd_knhashmask = 0;
	}

	fdnode = newfdp->fd_files;
	for (i = newfdp->fd_lastfile; i-- >= 0; fdnode++) {
		if (fdnode->fp != NULL)
			fhold(fdnode->fp);
	}
	return (newfdp);
}

/*
 * Release a filedesc structure.
 */
void
fdfree(struct proc *p)
{
	struct thread *td = p->p_thread;
	struct filedesc *fdp = p->p_fd;
	struct fdnode *fdnode;
	int i;
	struct filedesc_to_leader *fdtol;
	struct file *fp;
	struct vnode *vp;
	struct flock lf;

	/* Certain daemons might not have file descriptors. */
	if (fdp == NULL)
		return;

	/* Check for special need to clear POSIX style locks */
	fdtol = p->p_fdtol;
	if (fdtol != NULL) {
		KASSERT(fdtol->fdl_refcount > 0,
			("filedesc_to_refcount botch: fdl_refcount=%d",
			 fdtol->fdl_refcount));
		if (fdtol->fdl_refcount == 1 &&
		    (p->p_leader->p_flag & P_ADVLOCK) != 0) {
			i = 0;
			fdnode = fdp->fd_files;
			for (i = 0; i <= fdp->fd_lastfile; i++, fdnode++) {
				if (fdnode->fp == NULL ||
				    fdnode->fp->f_type != DTYPE_VNODE)
					continue;
				fp = fdnode->fp;
				fhold(fp);
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
				fdrop(fp, p->p_thread);
				/* reload due to possible reallocation */
				fdnode = &fdp->fd_files[i];
			}
		}
	retry:
		if (fdtol->fdl_refcount == 1) {
			if (fdp->fd_holdleaderscount > 0 &&
			    (p->p_leader->p_flag & P_ADVLOCK) != 0) {
				/*
				 * close() or do_dup() has cleared a reference
				 * in a shared file descriptor table.
				 */
				fdp->fd_holdleaderswakeup = 1;
				tsleep(&fdp->fd_holdleaderscount,
				       0, "fdlhold", 0);
				goto retry;
			}
			if (fdtol->fdl_holdcount > 0) {
				/* 
				 * Ensure that fdtol->fdl_leader
				 * remains valid in closef().
				 */
				fdtol->fdl_wakeup = 1;
				tsleep(fdtol, 0, "fdlhold", 0);
				goto retry;
			}
		}
		fdtol->fdl_refcount--;
		if (fdtol->fdl_refcount == 0 &&
		    fdtol->fdl_holdcount == 0) {
			fdtol->fdl_next->fdl_prev = fdtol->fdl_prev;
			fdtol->fdl_prev->fdl_next = fdtol->fdl_next;
		} else
			fdtol = NULL;
		p->p_fdtol = NULL;
		if (fdtol != NULL)
			free(fdtol, M_FILEDESC_TO_LEADER);
	}
	if (--fdp->fd_refcnt > 0)
		return;
	/*
	 * we are the last reference to the structure, we can
	 * safely assume it will not change out from under us.
	 */
	for (i = 0; i <= fdp->fd_lastfile; ++i) {
		if (fdp->fd_files[i].fp)
			closef(fdp->fd_files[i].fp, td);
	}
	if (fdp->fd_files != fdp->fd_builtin_files)
		free(fdp->fd_files, M_FILEDESC);
	if (fdp->fd_cdir) {
		cache_drop(fdp->fd_ncdir);
		vrele(fdp->fd_cdir);
	}
	if (fdp->fd_rdir) {
		cache_drop(fdp->fd_nrdir);
		vrele(fdp->fd_rdir);
	}
	if (fdp->fd_jdir) {
		cache_drop(fdp->fd_njdir);
		vrele(fdp->fd_jdir);
	}
	if (fdp->fd_knlist)
		free(fdp->fd_knlist, M_KQUEUE);
	if (fdp->fd_knhash)
		free(fdp->fd_knhash, M_KQUEUE);
	free(fdp, M_FILEDESC);
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
 */
void
setugidsafety(struct proc *p)
{
	struct thread *td = p->p_thread;
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

#if 0
			if ((fdp->fd_files[i].fileflags & UF_MAPPED) != 0)
				(void) munmapfd(p, i);
#endif
			if (i < fdp->fd_knlistsize)
				knote_fdclose(p, i);
			/*
			 * NULL-out descriptor prior to close to avoid
			 * a race while close blocks.
			 */
			fp = fdp->fd_files[i].fp;
			funsetfd(fdp, i);
			closef(fp, td);
		}
	}
	while (fdp->fd_lastfile > 0 && fdp->fd_files[fdp->fd_lastfile].fp == NULL)
		fdp->fd_lastfile--;
}

/*
 * Close any files on exec?
 */
void
fdcloseexec(struct proc *p)
{
	struct thread *td = p->p_thread;
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

#if 0
			if (fdp->fd_files[i].fileflags & UF_MAPPED)
				(void) munmapfd(p, i);
#endif
			if (i < fdp->fd_knlistsize)
				knote_fdclose(p, i);
			/*
			 * NULL-out descriptor prior to close to avoid
			 * a race while close blocks.
			 */
			fp = fdp->fd_files[i].fp;
			funsetfd(fdp, i);
			closef(fp, td);
		}
	}
	while (fdp->fd_lastfile > 0 && fdp->fd_files[fdp->fd_lastfile].fp == NULL)
		fdp->fd_lastfile--;
}

/*
 * It is unsafe for set[ug]id processes to be started with file
 * descriptors 0..2 closed, as these descriptors are given implicit
 * significance in the Standard C library.  fdcheckstd() will create a
 * descriptor referencing /dev/null for each of stdin, stdout, and
 * stderr that is not already open.
 */
int
fdcheckstd(struct proc *p)
{
	struct thread *td = p->p_thread;
	struct nlookupdata nd;
	struct filedesc *fdp;
	struct file *fp;
	register_t retval;
	int fd, i, error, flags, devnull;

       fdp = p->p_fd;
       if (fdp == NULL)
               return (0);
       devnull = -1;
       error = 0;
       for (i = 0; i < 3; i++) {
		if (fdp->fd_files[i].fp != NULL)
			continue;
		if (devnull < 0) {
			if ((error = falloc(p, &fp, NULL)) != 0)
				break;

			error = nlookup_init(&nd, "/dev/null", UIO_SYSSPACE,
						NLC_FOLLOW|NLC_LOCKVP);
			flags = FREAD | FWRITE;
			if (error == 0)
				error = vn_open(&nd, fp, flags, 0);
			if (error == 0)
				error = fsetfd(p, fp, &fd);
			fdrop(fp, td);
			nlookup_done(&nd);
			if (error)
				break;
			KKASSERT(i == fd);
			devnull = fd;
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
 */
int
closef(struct file *fp, struct thread *td)
{
	struct vnode *vp;
	struct flock lf;
	struct filedesc_to_leader *fdtol;
	struct proc *p;

	if (fp == NULL)
		return (0);
	if (td == NULL) {
		td = curthread;
		p = NULL;		/* allow no proc association */
	} else {
		p = td->td_proc;	/* can also be NULL */
	}
	/*
	 * POSIX record locking dictates that any close releases ALL
	 * locks owned by this process.  This is handled by setting
	 * a flag in the unlock to free ONLY locks obeying POSIX
	 * semantics, and not to free BSD-style file locks.
	 * If the descriptor was in a message, POSIX-style locks
	 * aren't passed with the descriptor.
	 */
	if (p != NULL && 
	    fp->f_type == DTYPE_VNODE) {
		if ((p->p_leader->p_flag & P_ADVLOCK) != 0) {
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
			/*
			 * Handle special case where file descriptor table
			 * is shared between multiple process leaders.
			 */
			for (fdtol = fdtol->fdl_next;
			     fdtol != p->p_fdtol;
			     fdtol = fdtol->fdl_next) {
				if ((fdtol->fdl_leader->p_flag &
				     P_ADVLOCK) == 0)
					continue;
				fdtol->fdl_holdcount++;
				lf.l_whence = SEEK_SET;
				lf.l_start = 0;
				lf.l_len = 0;
				lf.l_type = F_UNLCK;
				vp = (struct vnode *)fp->f_data;
				(void) VOP_ADVLOCK(vp,
						   (caddr_t)p->p_leader,
						   F_UNLCK, &lf, F_POSIX);
				fdtol->fdl_holdcount--;
				if (fdtol->fdl_holdcount == 0 &&
				    fdtol->fdl_wakeup != 0) {
					fdtol->fdl_wakeup = 0;
					wakeup(fdtol);
				}
			}
		}
	}
	return (fdrop(fp, td));
}

int
fdrop(struct file *fp, struct thread *td)
{
	struct flock lf;
	struct vnode *vp;
	int error;

	if (--fp->f_count > 0)
		return (0);
	if (fp->f_count < 0)
		panic("fdrop: count < 0");
	if ((fp->f_flag & FHASLOCK) && fp->f_type == DTYPE_VNODE) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		lf.l_type = F_UNLCK;
		vp = (struct vnode *)fp->f_data;
		(void) VOP_ADVLOCK(vp, (caddr_t)fp, F_UNLCK, &lf, F_FLOCK);
	}
	if (fp->f_ops != &badfileops)
		error = fo_close(fp, td);
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
 */
/* ARGSUSED */
int
flock(struct flock_args *uap)
{
	struct proc *p = curproc;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct vnode *vp;
	struct flock lf;

	if ((unsigned)uap->fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_files[uap->fd].fp) == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_VNODE)
		return (EOPNOTSUPP);
	vp = (struct vnode *)fp->f_data;
	lf.l_whence = SEEK_SET;
	lf.l_start = 0;
	lf.l_len = 0;
	if (uap->how & LOCK_UN) {
		lf.l_type = F_UNLCK;
		fp->f_flag &= ~FHASLOCK;
		return (VOP_ADVLOCK(vp, (caddr_t)fp, F_UNLCK, &lf, F_FLOCK));
	}
	if (uap->how & LOCK_EX)
		lf.l_type = F_WRLCK;
	else if (uap->how & LOCK_SH)
		lf.l_type = F_RDLCK;
	else
		return (EBADF);
	fp->f_flag |= FHASLOCK;
	if (uap->how & LOCK_NB)
		return (VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, F_FLOCK));
	return (VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, F_FLOCK|F_WAIT));
}

/*
 * File Descriptor pseudo-device driver (/dev/fd/).
 *
 * Opening minor device N dup()s the file (if any) connected to file
 * descriptor N belonging to the calling process.  Note that this driver
 * consists of only the ``open()'' routine, because all subsequent
 * references to this file will be direct to the other driver.
 */
/* ARGSUSED */
static int
fdopen(dev_t dev, int mode, int type, struct thread *td)
{
	KKASSERT(td->td_lwp != NULL);

	/*
	 * XXX Kludge: set curlwp->lwp_dupfd to contain the value of the
	 * the file descriptor being sought for duplication. The error
	 * return ensures that the vnode for this device will be released
	 * by vn_open. Open will detect this special error and take the
	 * actions in dupfdopen below. Other callers of vn_open or VOP_OPEN
	 * will simply report the error.
	 */
	td->td_lwp->lwp_dupfd = minor(dev);
	return (ENODEV);
}

/*
 * Duplicate the specified descriptor to a free descriptor.
 */
int
dupfdopen(struct filedesc *fdp, int indx, int dfd, int mode, int error)
{
	struct file *wfp;
	struct file *fp;

	/*
	 * If the to-be-dup'd fd number is greater than the allowed number
	 * of file descriptors, or the fd to be dup'd has already been
	 * closed, then reject.
	 */
	if ((u_int)dfd >= fdp->fd_nfiles ||
	    (wfp = fdp->fd_files[dfd].fp) == NULL) {
		return (EBADF);
	}

	/*
	 * There are two cases of interest here.
	 *
	 * For ENODEV simply dup (dfd) to file descriptor
	 * (indx) and return.
	 *
	 * For ENXIO steal away the file structure from (dfd) and
	 * store it in (indx).  (dfd) is effectively closed by
	 * this operation.
	 *
	 * Any other error code is just returned.
	 */
	switch (error) {
	case ENODEV:
		/*
		 * Check that the mode the file is being opened for is a
		 * subset of the mode of the existing descriptor.
		 */
		if (((mode & (FREAD|FWRITE)) | wfp->f_flag) != wfp->f_flag)
			return (EACCES);
		fp = fdp->fd_files[indx].fp;
#if 0
		if (fp && fdp->fd_files[indx].fileflags & UF_MAPPED)
			(void) munmapfd(p, indx);
#endif
		fdp->fd_files[indx].fp = wfp;
		fdp->fd_files[indx].fileflags = fdp->fd_files[dfd].fileflags;
		fhold(wfp);
		if (indx > fdp->fd_lastfile)
			fdp->fd_lastfile = indx;
		/*
		 * we now own the reference to fp that the ofiles[] array
		 * used to own.  Release it.
		 */
		if (fp)
			fdrop(fp, curthread);
		return (0);

	case ENXIO:
		/*
		 * Steal away the file pointer from dfd, and stuff it into indx.
		 */
		fp = fdp->fd_files[indx].fp;
#if 0
		if (fp && fdp->fd_files[indx].fileflags & UF_MAPPED)
			(void) munmapfd(p, indx);
#endif
		fdp->fd_files[indx].fp = fdp->fd_files[dfd].fp;
		fdp->fd_files[indx].fileflags = fdp->fd_files[dfd].fileflags;
		funsetfd(fdp, dfd);

		/*
		 * we now own the reference to fp that the files[] array
		 * used to own.  Release it.
		 */
		if (fp)
			fdrop(fp, curthread);
		/*
		 * Complete the clean up of the filedesc structure by
		 * recomputing the various hints.
		 */
		if (indx > fdp->fd_lastfile) {
			fdp->fd_lastfile = indx;
		} else {
			while (fdp->fd_lastfile > 0 &&
			   fdp->fd_files[fdp->fd_lastfile].fp == NULL) {
				fdp->fd_lastfile--;
			}
		}
		return (0);

	default:
		return (error);
	}
	/* NOTREACHED */
}


struct filedesc_to_leader *
filedesc_to_leader_alloc(struct filedesc_to_leader *old,
			 struct proc *leader)
{
	struct filedesc_to_leader *fdtol;
	
	fdtol = malloc(sizeof(struct filedesc_to_leader), 
			M_FILEDESC_TO_LEADER, M_WAITOK);
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
 * Get file structures.
 */
static int
sysctl_kern_file(SYSCTL_HANDLER_ARGS)
{
	struct kinfo_file kf;
	struct filedesc *fdp;
	struct file *fp;
	struct proc *p;
	int count;
	int error;
	int n;

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
	 */
	count = 0;
	error = 0;
	LIST_FOREACH(p, &allproc, p_list) {
		if (p->p_stat == SIDL)
			continue;
		if (!PRISON_CHECK(req->td->td_proc->p_ucred, p->p_ucred) != 0)
			continue;
		if ((fdp = p->p_fd) == NULL)
			continue;
		for (n = 0; n < fdp->fd_nfiles; ++n) {
			if ((fp = fdp->fd_files[n].fp) == NULL)
				continue;
			if (req->oldptr == NULL) {
				++count;
			} else {
				kcore_make_file(&kf, fp, p->p_pid,
						p->p_ucred->cr_uid, n);
				error = SYSCTL_OUT(req, &kf, sizeof(kf));
				if (error)
					break;
			}
		}
		if (error)
			break;
	}

	/*
	 * When just calculating the size, overestimate a bit to try to
	 * prevent system activity from causing the buffer-fill call 
	 * to fail later on.
	 */
	if (req->oldptr == NULL) {
		count = (count + 16) + (count / 10);
		error = SYSCTL_OUT(req, NULL, count * sizeof(kf));
	}
	return (error);
}

SYSCTL_PROC(_kern, KERN_FILE, file, CTLTYPE_OPAQUE|CTLFLAG_RD,
    0, 0, sysctl_kern_file, "S,file", "Entire file table");

SYSCTL_INT(_kern, KERN_MAXFILESPERPROC, maxfilesperproc, CTLFLAG_RW, 
    &maxfilesperproc, 0, "Maximum files allowed open per process");

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

	cdevsw_add(&fildesc_cdevsw, 0, 0);
	for (fd = 0; fd < NUMFDESC; fd++) {
		make_dev(&fildesc_cdevsw, fd,
		    UID_BIN, GID_BIN, 0666, "fd/%d", fd);
	}
	make_dev(&fildesc_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "stdin");
	make_dev(&fildesc_cdevsw, 1, UID_ROOT, GID_WHEEL, 0666, "stdout");
	make_dev(&fildesc_cdevsw, 2, UID_ROOT, GID_WHEEL, 0666, "stderr");
}

struct fileops badfileops = {
	NULL,	/* port */
	NULL,	/* clone */
	badfo_readwrite,
	badfo_readwrite,
	badfo_ioctl,
	badfo_poll,
	badfo_kqfilter,
	badfo_stat,
	badfo_close,
	badfo_shutdown
};

static int
badfo_readwrite(
	struct file *fp,
	struct uio *uio,
	struct ucred *cred,
	int flags,
	struct thread *td
) {
	return (EBADF);
}

static int
badfo_ioctl(struct file *fp, u_long com, caddr_t data, struct thread *td)
{
	return (EBADF);
}

static int
badfo_poll(struct file *fp, int events, struct ucred *cred, struct thread *td)
{
	return (0);
}

static int
badfo_kqfilter(struct file *fp, struct knote *kn)
{
	return (0);
}

static int
badfo_stat(struct file *fp, struct stat *sb, struct thread *td)
{
	return (EBADF);
}

static int
badfo_close(struct file *fp, struct thread *td)
{
	return (EBADF);
}

static int
badfo_shutdown(struct file *fp, int how, struct thread *td)
{
	return (EBADF);
}

int
nofo_shutdown(struct file *fp, int how, struct thread *td)
{
	return (EOPNOTSUPP);
}

SYSINIT(fildescdev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR,
					fildesc_drvinit,NULL)
