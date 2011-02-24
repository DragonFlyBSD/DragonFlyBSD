/*
 * Copyright (c) 1993, 1995 Jan-Simon Pendry
 * Copyright (c) 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
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
 *	@(#)procfs_vnops.c	8.18 (Berkeley) 5/21/95
 *
 * $FreeBSD: src/sys/miscfs/procfs/procfs_vnops.c,v 1.76.2.7 2002/01/22 17:22:59 nectar Exp $
 */

/*
 * procfs vnode interface
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/dirent.h>
#include <sys/malloc.h>
#include <sys/reg.h>
#include <vm/vm_zone.h>
#include <vfs/procfs/procfs.h>
#include <sys/pioctl.h>

#include <sys/spinlock2.h>

#include <machine/limits.h>

static int	procfs_access (struct vop_access_args *);
static int	procfs_badop (struct vop_generic_args *);
static int	procfs_bmap (struct vop_bmap_args *);
static int	procfs_close (struct vop_close_args *);
static int	procfs_getattr (struct vop_getattr_args *);
static int	procfs_inactive (struct vop_inactive_args *);
static int	procfs_ioctl (struct vop_ioctl_args *);
static int	procfs_lookup (struct vop_old_lookup_args *);
static int	procfs_open (struct vop_open_args *);
static int	procfs_print (struct vop_print_args *);
static int	procfs_readdir (struct vop_readdir_args *);
static int	procfs_readlink (struct vop_readlink_args *);
static int	procfs_reclaim (struct vop_reclaim_args *);
static int	procfs_setattr (struct vop_setattr_args *);

static int	procfs_readdir_proc(struct vop_readdir_args *);
static int	procfs_readdir_root(struct vop_readdir_args *);

/*
 * procfs vnode operations.
 */
struct vop_ops procfs_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		procfs_access,
	.vop_advlock =		(void *)procfs_badop,
	.vop_bmap =		procfs_bmap,
	.vop_close =		procfs_close,
	.vop_old_create =	(void *)procfs_badop,
	.vop_getattr =		procfs_getattr,
	.vop_inactive =		procfs_inactive,
	.vop_old_link =		(void *)procfs_badop,
	.vop_old_lookup =	procfs_lookup,
	.vop_old_mkdir =	(void *)procfs_badop,
	.vop_old_mknod =	(void *)procfs_badop,
	.vop_open =		procfs_open,
	.vop_pathconf =		vop_stdpathconf,
	.vop_print =		procfs_print,
	.vop_read =		procfs_rw,
	.vop_readdir =		procfs_readdir,
	.vop_readlink =		procfs_readlink,
	.vop_reclaim =		procfs_reclaim,
	.vop_old_remove =	(void *)procfs_badop,
	.vop_old_rename =	(void *)procfs_badop,
	.vop_old_rmdir =	(void *)procfs_badop,
	.vop_setattr =		procfs_setattr,
	.vop_old_symlink =	(void *)procfs_badop,
	.vop_write =		(void *)procfs_rw,
	.vop_ioctl =		procfs_ioctl
};


/*
 * This is a list of the valid names in the
 * process-specific sub-directories.  It is
 * used in procfs_lookup and procfs_readdir
 */
static struct proc_target {
	u_char	pt_type;
	u_char	pt_namlen;
	char	*pt_name;
	pfstype	pt_pfstype;
	int	(*pt_valid) (struct lwp *p);
} proc_targets[] = {
#define N(s) sizeof(s)-1, s
	/*	  name		type		validp */
	{ DT_DIR, N("."),	Pproc,		NULL },
	{ DT_DIR, N(".."),	Proot,		NULL },
	{ DT_REG, N("mem"),	Pmem,		NULL },
	{ DT_REG, N("regs"),	Pregs,		procfs_validregs },
	{ DT_REG, N("fpregs"),	Pfpregs,	procfs_validfpregs },
	{ DT_REG, N("dbregs"),	Pdbregs,	procfs_validdbregs },
	{ DT_REG, N("ctl"),	Pctl,		NULL },
	{ DT_REG, N("status"),	Pstatus,	NULL },
	{ DT_REG, N("note"),	Pnote,		NULL },
	{ DT_REG, N("notepg"),	Pnotepg,	NULL },
	{ DT_REG, N("map"), 	Pmap,		procfs_validmap },
	{ DT_REG, N("etype"),	Ptype,		procfs_validtype },
	{ DT_REG, N("cmdline"),	Pcmdline,	NULL },
	{ DT_REG, N("rlimit"),	Prlimit,	NULL },
	{ DT_LNK, N("file"),	Pfile,		NULL },
#undef N
};
static const int nproc_targets = NELEM(proc_targets);

static pid_t atopid (const char *, u_int);

/*
 * set things up for doing i/o on
 * the pfsnode (vp).  (vp) is locked
 * on entry, and should be left locked
 * on exit.
 *
 * for procfs we don't need to do anything
 * in particular for i/o.  all that is done
 * is to support exclusive open on process
 * memory images.
 *
 * procfs_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	       struct file *a_fp)
 */
static int
procfs_open(struct vop_open_args *ap)
{
	struct pfsnode *pfs = VTOPFS(ap->a_vp);
	struct proc *p1, *p2;
	int error;

	p2 = pfs_pfind(pfs->pfs_pid);
	if (p2 == NULL)
		return (ENOENT);
	if (pfs->pfs_pid && !PRISON_CHECK(ap->a_cred, p2->p_ucred)) {
		error = ENOENT;
		goto done;
	}

	switch (pfs->pfs_type) {
	case Pmem:
		if (((pfs->pfs_flags & FWRITE) && (ap->a_mode & O_EXCL)) ||
		    ((pfs->pfs_flags & O_EXCL) && (ap->a_mode & FWRITE))) {
			error = EBUSY;
			goto done;
		}

		p1 = curproc;
		KKASSERT(p1);
		/* Can't trace a process that's currently exec'ing. */ 
		if ((p2->p_flag & P_INEXEC) != 0) {
			error = EAGAIN;
			goto done;
		}
		if (!CHECKIO(p1, p2) || p_trespass(ap->a_cred, p2->p_ucred)) {
			error = EPERM;
			goto done;
		}

		if (ap->a_mode & FWRITE)
			pfs->pfs_flags = ap->a_mode & (FWRITE|O_EXCL);

		break;

	default:
		break;
	}
	error = vop_stdopen(ap);
done:
	PRELE(p2);
	return error;
}

/*
 * close the pfsnode (vp) after doing i/o.
 * (vp) is not locked on entry or exit.
 *
 * nothing to do for procfs other than undo
 * any exclusive open flag (see _open above).
 *
 * procfs_close(struct vnode *a_vp, int a_fflag, struct ucred *a_cred)
 */
static int
procfs_close(struct vop_close_args *ap)
{
	struct pfsnode *pfs = VTOPFS(ap->a_vp);
	struct proc *p;

	switch (pfs->pfs_type) {
	case Pmem:
		if ((ap->a_fflag & FWRITE) && (pfs->pfs_flags & O_EXCL))
			pfs->pfs_flags &= ~(FWRITE|O_EXCL);
		/*
		 * v_opencount determines the last real close on the vnode.
		 *
		 * If this is the last close, then it checks to see if
		 * the target process has PF_LINGER set in p_pfsflags,
		 * if this is *not* the case, then the process' stop flags
		 * are cleared, and the process is woken up.  This is
		 * to help prevent the case where a process has been
		 * told to stop on an event, but then the requesting process
		 * has gone away or forgotten about it.
		 */
		p = NULL;
		if ((ap->a_vp->v_opencount < 2)
		    && (p = pfind(pfs->pfs_pid))
		    && !(p->p_pfsflags & PF_LINGER)) {
			spin_lock(&p->p_spin);
			p->p_stops = 0;
			p->p_step = 0;
			spin_unlock(&p->p_spin);
			wakeup(&p->p_step);
		}
		if (p)
			PRELE(p);
		break;
	default:
		break;
	}

	return (vop_stdclose(ap));
}

/*
 * do an ioctl operation on a pfsnode (vp).
 * (vp) is not locked on entry or exit.
 */
static int
procfs_ioctl(struct vop_ioctl_args *ap)
{
	struct pfsnode *pfs = VTOPFS(ap->a_vp);
	struct proc *procp;
	struct proc *p;
	int error;
	int signo;
	struct procfs_status *psp;
	unsigned char flags;

	procp = pfind(pfs->pfs_pid);
	if (procp == NULL)
		return ENOTTY;
	p = curproc;
	if (p == NULL) {
		error = EINVAL;
		goto done;
	}

	/* Can't trace a process that's currently exec'ing. */ 
	if ((procp->p_flag & P_INEXEC) != 0) {
		error = EAGAIN;
		goto done;
	}
	if (!CHECKIO(p, procp) || p_trespass(ap->a_cred, procp->p_ucred)) {
		error = EPERM;
		goto done;
	}

	switch (ap->a_command) {
	case PIOCBIS:
	  procp->p_stops |= *(unsigned int*)ap->a_data;
	  break;
	case PIOCBIC:
	  procp->p_stops &= ~*(unsigned int*)ap->a_data;
	  break;
	case PIOCSFL:
	  /*
	   * NFLAGS is "non-suser_xxx flags" -- currently, only
	   * PFS_ISUGID ("ignore set u/g id");
	   */
#define NFLAGS	(PF_ISUGID)
	  flags = (unsigned char)*(unsigned int*)ap->a_data;
	  if (flags & NFLAGS && (error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0)))
	    goto done;
	  procp->p_pfsflags = flags;
	  break;
	case PIOCGFL:
	  *(unsigned int*)ap->a_data = (unsigned int)procp->p_pfsflags;
	  break;
	case PIOCSTATUS:
	  /*
	   * NOTE: syscall entry deals with stopevents and may run without
	   *	   the MP lock.
	   */
	  psp = (struct procfs_status *)ap->a_data;
	  psp->flags = procp->p_pfsflags;
	  psp->events = procp->p_stops;
	  spin_lock(&procp->p_spin);
	  if (procp->p_step) {
	    psp->state = 0;
	    psp->why = procp->p_stype;
	    psp->val = procp->p_xstat;
	    spin_unlock(&procp->p_spin);
	  } else {
	    psp->state = 1;
	    spin_unlock(&procp->p_spin);
	    psp->why = 0;	/* Not defined values */
	    psp->val = 0;	/* Not defined values */
	  }
	  break;
	case PIOCWAIT:
	  /*
	   * NOTE: syscall entry deals with stopevents and may run without
	   *	   the MP lock.
	   */
	  psp = (struct procfs_status *)ap->a_data;
	  spin_lock(&procp->p_spin);
	  while (procp->p_step == 0) {
	    tsleep_interlock(&procp->p_stype, PCATCH);
	    spin_unlock(&procp->p_spin);
	    error = tsleep(&procp->p_stype, PCATCH | PINTERLOCKED, "piocwait", 0);
	    if (error)
	      goto done;
	    spin_lock(&procp->p_spin);
	  }
	  spin_unlock(&procp->p_spin);
	  psp->state = 1;	/* It stopped */
	  psp->flags = procp->p_pfsflags;
	  psp->events = procp->p_stops;
	  psp->why = procp->p_stype;	/* why it stopped */
	  psp->val = procp->p_xstat;	/* any extra info */
	  break;
	case PIOCCONT:	/* Restart a proc */
	  /*
	   * NOTE: syscall entry deals with stopevents and may run without
	   *	   the MP lock.  However, the caller is presumably interlocked
	   *	   by having waited.
	   */
	  if (procp->p_step == 0) {
	    error = EINVAL;	/* Can only start a stopped process */
	    goto done;
	  }
	  if ((signo = *(int*)ap->a_data) != 0) {
	    if (signo >= NSIG || signo <= 0) {
	      error = EINVAL;
	      goto done;
	    }
	    ksignal(procp, signo);
	  }
	  procp->p_step = 0;
	  wakeup(&procp->p_step);
	  break;
	default:
	  error = ENOTTY;
	  goto done;
	}
	error = 0;
done:
	PRELE(procp);
	return 0;
}

/*
 * do block mapping for pfsnode (vp).
 * since we don't use the buffer cache
 * for procfs this function should never
 * be called.  in any case, it's not clear
 * what part of the kernel ever makes use
 * of this function.  for sanity, this is the
 * usual no-op bmap, although returning
 * (EIO) would be a reasonable alternative.
 *
 * XXX mmap assumes buffer cache operation
 *
 * procfs_bmap(struct vnode *a_vp, off_t a_loffset,
 *		off_t *a_doffsetp, int *a_runp, int *a_runb)
 */
static int
procfs_bmap(struct vop_bmap_args *ap)
{
	if (ap->a_doffsetp != NULL)
		*ap->a_doffsetp = ap->a_loffset;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;
	return (0);
}

/*
 * procfs_inactive is called when the pfsnode
 * is vrele'd and the reference count goes
 * to zero.  (vp) will be on the vnode free
 * list, so to get it back vget() must be
 * used.
 *
 * (vp) is locked on entry, but must be unlocked on exit.
 *
 * procfs_inactive(struct vnode *a_vp)
 */
static int
procfs_inactive(struct vop_inactive_args *ap)
{
	struct pfsnode *pfs = VTOPFS(ap->a_vp);

	if (pfs->pfs_pid & PFS_DEAD)
		vrecycle(ap->a_vp);
	return (0);
}

/*
 * _reclaim is called when getnewvnode()
 * wants to make use of an entry on the vnode
 * free list.  at this time the filesystem needs
 * to free any private data and remove the node
 * from any private lists.
 *
 * procfs_reclaim(struct vnode *a_vp)
 */
static int
procfs_reclaim(struct vop_reclaim_args *ap)
{
	return (procfs_freevp(ap->a_vp));
}

/*
 * _print is used for debugging.
 * just print a readable description
 * of (vp).
 *
 * procfs_print(struct vnode *a_vp)
 */
static int
procfs_print(struct vop_print_args *ap)
{
	struct pfsnode *pfs = VTOPFS(ap->a_vp);

	kprintf("tag VT_PROCFS, type %d, pid %ld, mode %x, flags %lx\n",
	    pfs->pfs_type, (long)pfs->pfs_pid, pfs->pfs_mode, pfs->pfs_flags);
	return (0);
}

/*
 * generic entry point for unsupported operations
 */
static int
procfs_badop(struct vop_generic_args *ap)
{
	return (EIO);
}

/*
 * Invent attributes for pfsnode (vp) and store
 * them in (vap).
 * Directories lengths are returned as zero since
 * any real length would require the genuine size
 * to be computed, and nothing cares anyway.
 *
 * this is relatively minimal for procfs.
 *
 * procfs_getattr(struct vnode *a_vp, struct vattr *a_vap)
 */
static int
procfs_getattr(struct vop_getattr_args *ap)
{
	struct pfsnode *pfs = VTOPFS(ap->a_vp);
	struct vattr *vap = ap->a_vap;
	struct proc *procp;
	int error;

	/*
	 * First make sure that the process and its credentials 
	 * still exist.
	 */
	switch (pfs->pfs_type) {
	case Proot:
	case Pcurproc:
		procp = NULL;
		break;

	default:
		procp = pfs_pfind(pfs->pfs_pid);
		if (procp == NULL || procp->p_ucred == NULL) {
			error = ENOENT;
			goto done;
		}
	}

	error = 0;

	/* start by zeroing out the attributes */
	VATTR_NULL(vap);

	/* next do all the common fields */
	vap->va_type = ap->a_vp->v_type;
	vap->va_mode = pfs->pfs_mode;
	vap->va_fileid = pfs->pfs_fileno;
	vap->va_flags = 0;
	vap->va_blocksize = PAGE_SIZE;
	vap->va_bytes = vap->va_size = 0;
	vap->va_fsid = ap->a_vp->v_mount->mnt_stat.f_fsid.val[0];

	/*
	 * Make all times be current TOD.
	 * It would be possible to get the process start
	 * time from the p_stat structure, but there's
	 * no "file creation" time stamp anyway, and the
	 * p_stat structure is not addressible if u. gets
	 * swapped out for that process.
	 */
	nanotime(&vap->va_ctime);
	vap->va_atime = vap->va_mtime = vap->va_ctime;

	/*
	 * If the process has exercised some setuid or setgid
	 * privilege, then rip away read/write permission so
	 * that only root can gain access.
	 */
	switch (pfs->pfs_type) {
	case Pctl:
	case Pregs:
	case Pfpregs:
	case Pdbregs:
	case Pmem:
		if (procp->p_flag & P_SUGID)
			vap->va_mode &= ~((VREAD|VWRITE)|
					  ((VREAD|VWRITE)>>3)|
					  ((VREAD|VWRITE)>>6));
		break;
	default:
		break;
	}

	/*
	 * now do the object specific fields
	 *
	 * The size could be set from struct reg, but it's hardly
	 * worth the trouble, and it puts some (potentially) machine
	 * dependent data into this machine-independent code.  If it
	 * becomes important then this function should break out into
	 * a per-file stat function in the corresponding .c file.
	 */

	vap->va_nlink = 1;
	if (procp) {
		vap->va_uid = procp->p_ucred->cr_uid;
		vap->va_gid = procp->p_ucred->cr_gid;
	}

	switch (pfs->pfs_type) {
	case Proot:
		/*
		 * Set nlink to 1 to tell fts(3) we don't actually know.
		 */
		vap->va_nlink = 1;
		vap->va_uid = 0;
		vap->va_gid = 0;
		vap->va_size = vap->va_bytes = DEV_BSIZE;
		break;

	case Pcurproc: {
		char buf[16];		/* should be enough */
		vap->va_uid = 0;
		vap->va_gid = 0;
		vap->va_size = vap->va_bytes =
		    ksnprintf(buf, sizeof(buf), "%ld", (long)curproc->p_pid);
		break;
	}

	case Pproc:
		vap->va_nlink = nproc_targets;
		vap->va_size = vap->va_bytes = DEV_BSIZE;
		break;

	case Pfile: {
		char *fullpath, *freepath;
		error = cache_fullpath(procp, &procp->p_textnch, &fullpath, &freepath, 0);
		if (error == 0) {
			vap->va_size = strlen(fullpath);
			kfree(freepath, M_TEMP);
		} else {
			vap->va_size = sizeof("unknown") - 1;
			error = 0;
		}
		vap->va_bytes = vap->va_size;
		break;
	}

	case Pmem:
		/*
		 * If we denied owner access earlier, then we have to
		 * change the owner to root - otherwise 'ps' and friends
		 * will break even though they are setgid kmem. *SIGH*
		 */
		if (procp->p_flag & P_SUGID)
			vap->va_uid = 0;
		else
			vap->va_uid = procp->p_ucred->cr_uid;
		break;

	case Pregs:
		vap->va_bytes = vap->va_size = sizeof(struct reg);
		break;

	case Pfpregs:
		vap->va_bytes = vap->va_size = sizeof(struct fpreg);
		break;

        case Pdbregs:
                vap->va_bytes = vap->va_size = sizeof(struct dbreg);
                break;

	case Ptype:
	case Pmap:
	case Pctl:
	case Pstatus:
	case Pnote:
	case Pnotepg:
	case Pcmdline:
	case Prlimit:
		break;

	default:
		panic("procfs_getattr");
	}
done:
	if (procp)
		PRELE(procp);
	return (error);
}

/*
 * procfs_setattr(struct vnode *a_vp, struct vattr *a_vap,
 *		  struct ucred *a_cred)
 */
static int
procfs_setattr(struct vop_setattr_args *ap)
{
	if (ap->a_vap->va_flags != VNOVAL)
		return (EOPNOTSUPP);

	/*
	 * just fake out attribute setting
	 * it's not good to generate an error
	 * return, otherwise things like creat()
	 * will fail when they try to set the
	 * file length to 0.  worse, this means
	 * that echo $note > /proc/$pid/note will fail.
	 */

	return (0);
}

/*
 * implement access checking.
 *
 * procfs_access(struct vnode *a_vp, int a_mode, struct ucred *a_cred)
 */
static int
procfs_access(struct vop_access_args *ap)
{
	struct vattr vattr;
	int error;

	error = VOP_GETATTR(ap->a_vp, &vattr);
	if (!error)
		error = vop_helper_access(ap, vattr.va_uid, vattr.va_gid, 
				vattr.va_mode, 0);
	return (error);
}

/*
 * lookup.  this is incredibly complicated in the general case, however
 * for most pseudo-filesystems very little needs to be done.
 *
 * procfs_lookup(struct vnode *a_dvp, struct vnode **a_vpp,
 *		 struct componentname *a_cnp)
 */
static int
procfs_lookup(struct vop_old_lookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct vnode **vpp = ap->a_vpp;
	struct vnode *dvp = ap->a_dvp;
	char *pname = cnp->cn_nameptr;
	/* struct proc *curp = cnp->cn_proc; */
	struct proc_target *pt;
	pid_t pid;
	struct pfsnode *pfs;
	struct proc *p;
	struct lwp *lp;
	int i;
	int error;

	*vpp = NULL;

	if (cnp->cn_nameiop == NAMEI_DELETE || cnp->cn_nameiop == NAMEI_RENAME)
		return (EROFS);

	p = NULL;
	error = 0;
	if (cnp->cn_namelen == 1 && *pname == '.') {
		*vpp = dvp;
		vref(*vpp);
		goto out;
	}

	pfs = VTOPFS(dvp);
	switch (pfs->pfs_type) {
	case Proot:
		if (cnp->cn_flags & CNP_ISDOTDOT)
			return (EIO);

		if (CNEQ(cnp, "curproc", 7)) {
			error = procfs_allocvp(dvp->v_mount, vpp, 0, Pcurproc);
			goto out;
		}

		pid = atopid(pname, cnp->cn_namelen);
		if (pid == NO_PID)
			break;

		p = pfs_pfind(pid);
		if (p == NULL)
			break;

		if (!PRISON_CHECK(ap->a_cnp->cn_cred, p->p_ucred))
			break;

		if (ps_showallprocs == 0 && ap->a_cnp->cn_cred->cr_uid != 0 &&
		    ap->a_cnp->cn_cred->cr_uid != p->p_ucred->cr_uid)
			break;

		error = procfs_allocvp(dvp->v_mount, vpp, pid, Pproc);
		goto out;

	case Pproc:
		if (cnp->cn_flags & CNP_ISDOTDOT) {
			error = procfs_root(dvp->v_mount, vpp);
			goto out;
		}

		p = pfs_pfind(pfs->pfs_pid);
		if (p == NULL)
			break;
		/* XXX lwp */
		lp = FIRST_LWP_IN_PROC(p);

		if (!PRISON_CHECK(ap->a_cnp->cn_cred, p->p_ucred))
			break;

		if (ps_showallprocs == 0 && ap->a_cnp->cn_cred->cr_uid != 0 &&
		    ap->a_cnp->cn_cred->cr_uid != p->p_ucred->cr_uid)
			break;

		for (pt = proc_targets, i = 0; i < nproc_targets; pt++, i++) {
			if (cnp->cn_namelen == pt->pt_namlen &&
			    bcmp(pt->pt_name, pname, cnp->cn_namelen) == 0 &&
			    (pt->pt_valid == NULL || (*pt->pt_valid)(lp)))
				goto found;
		}
		break;
	found:
		error = procfs_allocvp(dvp->v_mount, vpp, pfs->pfs_pid,
					pt->pt_pfstype);
		goto out;

	default:
		error = ENOTDIR;
		goto out;
	}
	if (cnp->cn_nameiop == NAMEI_LOOKUP)
		error = ENOENT;
	else
		error = EROFS;
	/*
	 * If no error occured *vpp will hold a referenced locked vnode.
	 * dvp was passed to us locked and *vpp must be returned locked.
	 * If *vpp != dvp then we should unlock dvp if (1) this is not the
	 * last component or (2) CNP_LOCKPARENT is not set.
	 */
out:
	if (error == 0 && *vpp != dvp) {
		if ((cnp->cn_flags & CNP_LOCKPARENT) == 0) {
			cnp->cn_flags |= CNP_PDIRUNLOCK;
			vn_unlock(dvp);
		}
	}
	if (p)
		PRELE(p);
	return (error);
}

/*
 * Does this process have a text file?
 */
int
procfs_validfile(struct lwp *lp)
{
	return (procfs_findtextvp(lp->lwp_proc) != NULLVP);
}

/*
 * readdir() returns directory entries from pfsnode (vp).
 *
 * We generate just one directory entry at a time, as it would probably
 * not pay off to buffer several entries locally to save uiomove calls.
 *
 * procfs_readdir(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred,
 *		  int *a_eofflag, int *a_ncookies, off_t **a_cookies)
 */
static int
procfs_readdir(struct vop_readdir_args *ap)
{
	struct pfsnode *pfs;
	int error;

	if (ap->a_uio->uio_offset < 0 || ap->a_uio->uio_offset > INT_MAX)
		return (EINVAL);
	if ((error = vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY)) != 0)
		return (error);
	pfs = VTOPFS(ap->a_vp);

	switch (pfs->pfs_type) {
	case Pproc:
		/*
		 * this is for the process-specific sub-directories.
		 * all that is needed to is copy out all the entries
		 * from the procent[] table (top of this file).
		 */
		error = procfs_readdir_proc(ap);
		break;
	case Proot:
		/*
		 * this is for the root of the procfs filesystem
		 * what is needed is a special entry for "curproc"
		 * followed by an entry for each process on allproc
		 */
		error = procfs_readdir_root(ap);
		break;
	default:
		error = ENOTDIR;
		break;
	}

	vn_unlock(ap->a_vp);
	return (error);
}

static int
procfs_readdir_proc(struct vop_readdir_args *ap)
{
	struct pfsnode *pfs;
	int error, i, retval;
	struct proc *p;
	struct lwp *lp;
	struct proc_target *pt;
	struct uio *uio = ap->a_uio;

	pfs = VTOPFS(ap->a_vp);
	p = pfs_pfind(pfs->pfs_pid);
	if (p == NULL)
		return(0);
	if (!PRISON_CHECK(ap->a_cred, p->p_ucred)) {
		error = 0;
		goto done;
	}
	/* XXX lwp, not MPSAFE */
	lp = FIRST_LWP_IN_PROC(p);

	error = 0;
	i = (int)uio->uio_offset;
	if (i < 0) {
		error = EINVAL;
		goto done;
	}

	for (pt = &proc_targets[i];
	     !error && uio->uio_resid > 0 && i < nproc_targets; pt++, i++) {
		if (pt->pt_valid && (*pt->pt_valid)(lp) == 0)
			continue;

		retval = vop_write_dirent(&error, uio,
		    PROCFS_FILENO(pfs->pfs_pid, pt->pt_pfstype), pt->pt_type,
		    pt->pt_namlen, pt->pt_name);
		if (retval)
			break;
	}

	uio->uio_offset = (off_t)i;
	error = 0;
done:
	PRELE(p);
	return error;
}

struct procfs_readdir_root_info {
	int error;
	int i;
	int pcnt;
	struct uio *uio;
	struct ucred *cred;
};

static int procfs_readdir_root_callback(struct proc *p, void *data);

static int
procfs_readdir_root(struct vop_readdir_args *ap)
{
	struct procfs_readdir_root_info info;
	struct uio *uio = ap->a_uio;
	int res;

	info.error = 0;
	info.i = (int)uio->uio_offset;

	if (info.i < 0)
		return (EINVAL);

	info.pcnt = 0;
	info.uio = uio;
	info.cred = ap->a_cred;
	while (info.pcnt < 3) {
		res = procfs_readdir_root_callback(NULL, &info);
		if (res < 0)
			break;
	}
	if (res >= 0)
		allproc_scan(procfs_readdir_root_callback, &info);
	uio->uio_offset = (off_t)info.i;

	return (info.error);
}

static int
procfs_readdir_root_callback(struct proc *p, void *data)
{
	struct procfs_readdir_root_info *info = data;
	struct uio *uio;
	int retval;
	ino_t d_ino;
	const char *d_name;
	char d_name_pid[20];
	size_t d_namlen;
	uint8_t d_type;

	uio = info->uio;

	if (uio->uio_resid <= 0 || info->error)
		return(-1);

	switch (info->pcnt) {
	case 0:		/* `.' */
		d_ino = PROCFS_FILENO(0, Proot);
		d_name = ".";
		d_namlen = 1;
		d_type = DT_DIR;
		break;
	case 1:		/* `..' */
		d_ino = PROCFS_FILENO(0, Proot);
		d_name = "..";
		d_namlen = 2;
		d_type = DT_DIR;
		break;

	case 2:
		d_ino = PROCFS_FILENO(0, Pcurproc);
		d_namlen = 7;
		d_name = "curproc";
		d_type = DT_LNK;
		break;


	default:
		if (!PRISON_CHECK(info->cred, p->p_ucred))
			return(0);
		if (ps_showallprocs == 0 && 
		    info->cred->cr_uid != 0 &&
		    info->cred->cr_uid != p->p_ucred->cr_uid) {
			return(0);
		}

		/*
		 * Skip entries we have already returned (optimization)
		 */
		if (info->pcnt < info->i) {
			++info->pcnt;
			return(0);
		}

		d_ino = PROCFS_FILENO(p->p_pid, Pproc);
		d_namlen = ksnprintf(d_name_pid, sizeof(d_name_pid),
		    "%ld", (long)p->p_pid);
		d_name = d_name_pid;
		d_type = DT_DIR;
		break;
	}

	/*
	 * Skip entries we have already returned (optimization)
	 */
	if (info->pcnt < info->i) {
		++info->pcnt;
		return(0);
	}

	retval = vop_write_dirent(&info->error, uio,
				  d_ino, d_type, d_namlen, d_name);
	if (retval)
		return(-1);
	++info->pcnt;
	++info->i;
	return(0);
}

/*
 * readlink reads the link of `curproc' or `file'
 */
static int
procfs_readlink(struct vop_readlink_args *ap)
{
	char buf[16];		/* should be enough */
	struct proc *procp;
	struct vnode *vp = ap->a_vp;
	struct pfsnode *pfs = VTOPFS(vp);
	char *fullpath, *freepath;
	int error, len;

	switch (pfs->pfs_type) {
	case Pcurproc:
		if (pfs->pfs_fileno != PROCFS_FILENO(0, Pcurproc))
			return (EINVAL);

		len = ksnprintf(buf, sizeof(buf), "%ld", (long)curproc->p_pid);

		return (uiomove(buf, len, ap->a_uio));
	/*
	 * There _should_ be no way for an entire process to disappear
	 * from under us...
	 */
	case Pfile:
		procp = pfs_pfind(pfs->pfs_pid);
		if (procp == NULL || procp->p_ucred == NULL) {
			kprintf("procfs_readlink: pid %d disappeared\n",
			    pfs->pfs_pid);
			if (procp)
				PRELE(procp);
			return (uiomove("unknown", sizeof("unknown") - 1,
			    ap->a_uio));
		}
		error = cache_fullpath(procp, &procp->p_textnch, &fullpath, &freepath, 0);
		if (error != 0) {
			if (procp)
				PRELE(procp);
			return (uiomove("unknown", sizeof("unknown") - 1,
			    ap->a_uio));
		}
		error = uiomove(fullpath, strlen(fullpath), ap->a_uio);
		kfree(freepath, M_TEMP);
		if (procp)
			PRELE(procp);
		return (error);
	default:
		return (EINVAL);
	}
}

/*
 * convert decimal ascii to pid_t
 */
static pid_t
atopid(const char *b, u_int len)
{
	pid_t p = 0;

	while (len--) {
		char c = *b++;
		if (c < '0' || c > '9')
			return (NO_PID);
		p = 10 * p + (c - '0');
		if (p > PID_MAX)
			return (NO_PID);
	}

	return (p);
}

