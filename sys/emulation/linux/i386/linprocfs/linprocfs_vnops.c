/*
 * Copyright (c) 2000 Dag-Erling Coïdan Smørgrav
 * Copyright (c) 1999 Pierre Beyssac
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
 * $FreeBSD: src/sys/i386/linux/linprocfs/linprocfs_vnops.c,v 1.3.2.5 2001/08/12 14:29:19 rwatson Exp $
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
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/dirent.h>
#include <sys/malloc.h>
#include <sys/reg.h>
#include <sys/jail.h>
#include <vm/vm_zone.h>
#include "linprocfs.h"
#include <sys/pioctl.h>
#include <sys/spinlock2.h>

#include <machine/limits.h>

extern struct vnode *procfs_findtextvp (struct proc *);

static int	linprocfs_access (struct vop_access_args *);
static int	linprocfs_badop (struct vop_generic_args *);
static int	linprocfs_bmap (struct vop_bmap_args *);
static int	linprocfs_close (struct vop_close_args *);
static int	linprocfs_getattr (struct vop_getattr_args *);
static int	linprocfs_inactive (struct vop_inactive_args *);
static int	linprocfs_ioctl (struct vop_ioctl_args *);
static int	linprocfs_lookup (struct vop_old_lookup_args *);
static int	linprocfs_open (struct vop_open_args *);
static int	linprocfs_print (struct vop_print_args *);
static int	linprocfs_readdir (struct vop_readdir_args *);
static int	linprocfs_readlink (struct vop_readlink_args *);
static int	linprocfs_reclaim (struct vop_reclaim_args *);
static int	linprocfs_setattr (struct vop_setattr_args *);

static int	linprocfs_readdir_proc(struct vop_readdir_args *);
static int	linprocfs_readdir_root(struct vop_readdir_args *);
static int	linprocfs_readdir_net(struct vop_readdir_args *ap);
static int	linprocfs_readdir_sys(struct vop_readdir_args *ap);
static int	linprocfs_readdir_syskernel(struct vop_readdir_args *ap);

/*
 * procfs vnode operations.
 */
struct vop_ops linprocfs_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		linprocfs_access,
	.vop_advlock =		(void *)linprocfs_badop,
	.vop_bmap =		linprocfs_bmap,
	.vop_close =		linprocfs_close,
	.vop_old_create =	(void *)linprocfs_badop,
	.vop_getattr =		linprocfs_getattr,
	.vop_inactive =		linprocfs_inactive,
	.vop_old_link =		(void *)linprocfs_badop,
	.vop_old_lookup =	linprocfs_lookup,
	.vop_old_mkdir =	(void *)linprocfs_badop,
	.vop_old_mknod =	(void *)linprocfs_badop,
	.vop_open =		linprocfs_open,
	.vop_pathconf =		vop_stdpathconf,
	.vop_print =		linprocfs_print,
	.vop_read =		(void *)linprocfs_rw,
	.vop_readdir =		linprocfs_readdir,
	.vop_readlink =		linprocfs_readlink,
	.vop_reclaim =		linprocfs_reclaim,
	.vop_old_remove =	(void *)linprocfs_badop,
	.vop_old_rename =	(void *)linprocfs_badop,
	.vop_old_rmdir =	(void *)linprocfs_badop,
	.vop_setattr =		linprocfs_setattr,
	.vop_old_symlink =	(void *)linprocfs_badop,
	.vop_write =		(void *)linprocfs_rw,
	.vop_ioctl =		linprocfs_ioctl
};

/*
 * This is a list of the valid names in the
 * process-specific sub-directories.  It is
 * used in linprocfs_lookup and linprocfs_readdir
 */
static struct proc_target {
	u_char	pt_type;
	u_char	pt_namlen;
	char	*pt_name;
	pfstype	pt_pfstype;
	int	(*pt_valid) (struct proc *p);
} proc_targets[] = {
#define N(s) sizeof(s)-1, s
	/*	  name		type		validp */
	{ DT_DIR, N("."),	Pproc,		NULL },
	{ DT_DIR, N(".."),	Proot,		NULL },
	{ DT_REG, N("mem"),	Pmem,		NULL },

	{ DT_LNK, N("exe"),	Pexe,		NULL },
	{ DT_LNK, N("cwd"),	Pcwd,		NULL },
	{ DT_LNK, N("root"),	Pprocroot,	NULL },
	{ DT_LNK, N("fd"),	Pfd,		NULL },

	{ DT_REG, N("stat"),	Pprocstat,	NULL },
	{ DT_REG, N("status"),	Pprocstatus,	NULL },
	{ DT_REG, N("maps"),	Pmaps,		NULL },
	{ DT_REG, N("statm"),	Pstatm,		NULL },
#if 0
	{ DT_REG, N("cmdline"),	Pcmdline,	NULL },
	{ DT_REG, N("environ"),	Penviron,	NULL },
#endif
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
 */
static int
linprocfs_open(struct vop_open_args *ap)
{
	struct pfsnode *pfs = VTOPFS(ap->a_vp);
	struct proc *p2;
	int error;

	p2 = linprocfs_pfind(pfs->pfs_pid);
	if (p2 == NULL) {
		error = ENOENT;
	} else if (pfs->pfs_pid && !PRISON_CHECK(ap->a_cred, p2->p_ucred)) {
		error = ENOENT;
	} else {
		error = 0;

		switch (pfs->pfs_type) {
		case Pmem:
			if (((pfs->pfs_flags & FWRITE) &&
			     (ap->a_mode & O_EXCL)) ||
			    ((pfs->pfs_flags & O_EXCL) &&
			     (ap->a_mode & FWRITE))) {
				error = EBUSY;
				break;
			}

			if (p_trespass(ap->a_cred, p2->p_ucred)) {
				error = EPERM;
				break;
			}
			if (ap->a_mode & FWRITE)
				pfs->pfs_flags = ap->a_mode & (FWRITE|O_EXCL);
			break;
		default:
			break;
		}
	}
	if (error == 0)
		error = vop_stdopen(ap);
	if (p2)
		PRELE(p2);
	return error;
}

/*
 * close the pfsnode (vp) after doing i/o.
 * (vp) is not locked on entry or exit.
 *
 * nothing to do for procfs other than undo
 * any exclusive open flag (see _open above).
 */
static int
linprocfs_close(struct vop_close_args *ap)
{
	struct pfsnode *pfs = VTOPFS(ap->a_vp);
	struct proc *p;

	switch (pfs->pfs_type) {
	case Pmem:
		if ((ap->a_fflag & FWRITE) && (pfs->pfs_flags & O_EXCL))
			pfs->pfs_flags &= ~(FWRITE|O_EXCL);
		/*
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
		    && (p = linprocfs_pfind(pfs->pfs_pid))
		    && !(p->p_pfsflags & PF_LINGER)) {
			spin_lock(&p->p_spin);
			p->p_stops = 0;
			p->p_step = 0;
			spin_unlock(&p->p_spin);
			wakeup(&p->p_stype);
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
linprocfs_ioctl(struct vop_ioctl_args *ap)
{
	struct pfsnode *pfs = VTOPFS(ap->a_vp);
	struct proc *procp;
	int error;
	int signo;
	struct procfs_status *psp;
	unsigned char flags;

	procp = linprocfs_pfind(pfs->pfs_pid);
	if (procp == NULL)
		return ENOTTY;

	if (p_trespass(ap->a_cred, procp->p_ucred)) {
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
	case PIOCSTATUS:
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
	  psp = (struct procfs_status *)ap->a_data;
	  spin_lock(&procp->p_spin);
	  if (procp->p_step == 0) {
	    tsleep_interlock(&procp->p_stype, PCATCH);
	    spin_unlock(&procp->p_spin);
	    if (procp->p_stops == 0) {
		error = EINVAL;
		goto done;
	    }
	    if (procp->p_flags & P_POSTEXIT) {
		error = EINVAL;
		goto done;
	    }
	    if (procp->p_flags & P_INEXEC) {
		error = EAGAIN;
		goto done;
	    }
	    error = tsleep(&procp->p_stype, PCATCH | PINTERLOCKED,
			   "piocwait", 0);
	    if (error)
	      goto done;
	  } else {
	    spin_unlock(&procp->p_spin);
	  }
	  psp->state = 1;	/* It stopped */
	  psp->flags = procp->p_pfsflags;
	  psp->events = procp->p_stops;
	  psp->why = procp->p_stype;	/* why it stopped */
	  psp->val = procp->p_xstat;	/* any extra info */
	  break;
	case PIOCCONT:	/* Restart a proc */
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
	if (procp)
		PRELE(procp);
	return error;
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
 */
static int
linprocfs_bmap(struct vop_bmap_args *ap)
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
 * linprocfs_inactive is called when the pfsnode
 * is vrele'd and the reference count is about
 * to go to zero.  (vp) will be on the vnode free
 * list, so to get it back vget() must be
 * used.
 *
 * (vp) is locked on entry and must remain locked
 *      on exit.
 */
static int
linprocfs_inactive(struct vop_inactive_args *ap)
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
 */
static int
linprocfs_reclaim(struct vop_reclaim_args *ap)
{
	return (linprocfs_freevp(ap->a_vp));
}

/*
 * _print is used for debugging.
 * just print a readable description
 * of (vp).
 */
static int
linprocfs_print(struct vop_print_args *ap)
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
linprocfs_badop(struct vop_generic_args *ap __unused)
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
 */
static int
linprocfs_getattr(struct vop_getattr_args *ap)
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
	case Pself:
		procp = NULL;
		break;

	default:
		procp = linprocfs_pfind(pfs->pfs_pid);
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
	case Pnet:
	case Psys:
	case Psyskernel:
		/*
		 * Set nlink to 1 to tell fts(3) we don't actually know.
		 */
		vap->va_nlink = 1;
		vap->va_uid = 0;
		vap->va_gid = 0;
		vap->va_size = vap->va_bytes = DEV_BSIZE;
		break;

	case Pself: {
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

	case Pexe: {
		char *fullpath, *freepath;
		error = cache_fullpath(procp, &procp->p_textnch, NULL,
				       &fullpath, &freepath, 0);
		/* error = vn_fullpath(procp, NULL, &fullpath, &freepath); */
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
	case Pcwd: {
		char *fullpath, *freepath;
		error = cache_fullpath(procp, &procp->p_fd->fd_ncdir, NULL,
				       &fullpath, &freepath, 0);
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
	case Pprocroot: {
		struct nchandle *nchp;
		char *fullpath, *freepath;
		nchp = jailed(procp->p_ucred) ? &procp->p_fd->fd_njdir : &procp->p_fd->fd_nrdir;
		error = cache_fullpath(procp, nchp, NULL,
				       &fullpath, &freepath, 0);
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
	case Pfd: {
		if (procp == curproc) {
			vap->va_size = sizeof("/dev/fd") - 1;
			error = 0;	
		} else {
			vap->va_size = sizeof("unknown") - 1;
			error = 0;
		}
		vap->va_bytes = vap->va_size;
		break;
	}

	case Pmeminfo:
	case Pcpuinfo:
	case Pmounts:
	case Pstat:
	case Puptime:
	case Pversion:
	case Ploadavg:
	case Pnetdev:
	case Pdevices:
	case Posrelease:
	case Postype:
	case Ppidmax:
		vap->va_bytes = vap->va_size = 0;
		vap->va_uid = 0;
		vap->va_gid = 0;
		break;
		
	case Pmem:
		/*
		 * If we denied owner access earlier, then we have to
		 * change the owner to root - otherwise 'ps' and friends
		 * will break even though they are setgid kmem. *SIGH*
		 */
		if (procp->p_flags & P_SUGID)
			vap->va_uid = 0;
		else
			vap->va_uid = procp->p_ucred->cr_uid;
		break;

	case Pprocstat:
	case Pprocstatus:
	case Pcmdline:
	case Penviron:
	case Pmaps:
	case Pstatm:
		vap->va_bytes = vap->va_size = 0;
		/* uid, gid are already set */
		break;

	default:
		panic("linprocfs_getattr");
	}
done:
	if (procp)
		PRELE(procp);
	return (error);
}

static int
linprocfs_setattr(struct vop_setattr_args *ap)
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
 * something very similar to this code is duplicated
 * throughout the 4bsd kernel and should be moved
 * into kern/vfs_subr.c sometime.
 *
 * actually, the check for super-user is slightly
 * broken since it will allow read access to write-only
 * objects.  this doesn't cause any particular trouble
 * but does mean that the i/o entry points need to check
 * that the operation really does make sense.
 */
static int
linprocfs_access(struct vop_access_args *ap)
{
	struct vattr *vap;
	struct vattr vattr;
	int error;

	/*
	 * If you're the super-user,
	 * you always get access.
	 */
	if (ap->a_cred->cr_uid == 0)
		return (0);

	vap = &vattr;
	error = VOP_GETATTR(ap->a_vp, vap);
	if (error)
		return (error);

	/*
	 * Access check is based on only one of owner, group, public.
	 * If not owner, then check group. If not a member of the
	 * group, then check public access.
	 */
	if (ap->a_cred->cr_uid != vap->va_uid) {
		gid_t *gp;
		int i;

		ap->a_mode >>= 3;
		gp = ap->a_cred->cr_groups;
		for (i = 0; i < ap->a_cred->cr_ngroups; i++, gp++)
			if (vap->va_gid == *gp)
				goto found;
		ap->a_mode >>= 3;
found:
		;
	}

	if ((vap->va_mode & ap->a_mode) == ap->a_mode)
		return (0);

	return (EACCES);
}

/*
 * lookup.  this is incredibly complicated in the general case, however
 * for most pseudo-filesystems very little needs to be done.
 */
static int
linprocfs_lookup(struct vop_old_lookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct vnode **vpp = ap->a_vpp;
	struct vnode *dvp = ap->a_dvp;
	char *pname = cnp->cn_nameptr;
	struct proc_target *pt;
	pid_t pid;
	struct pfsnode *pfs;
	struct proc *p;
	int i;
	int error;

	*vpp = NULL;
	p = NULL;

	if (cnp->cn_nameiop == NAMEI_DELETE || 
	    cnp->cn_nameiop == NAMEI_RENAME ||
	    cnp->cn_nameiop == NAMEI_CREATE) {
		return (EROFS);
	}

	error = 0;

	if (cnp->cn_namelen == 1 && *pname == '.') {
		*vpp = dvp;
		vref(*vpp);
		goto out;
	}

	pfs = VTOPFS(dvp);
	switch (pfs->pfs_type) {
	case Psys:
		if (cnp->cn_flags & CNP_ISDOTDOT) {
			error = linprocfs_root(dvp->v_mount, vpp);
			goto out;
		}
		if (CNEQ(cnp, "kernel", 6)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Psyskernel);
			goto out;
		}		
		break;
	case Pnet:
		if (cnp->cn_flags & CNP_ISDOTDOT) {
			error = linprocfs_root(dvp->v_mount, vpp);
			goto out;
		}
		if (CNEQ(cnp, "dev", 3)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Pnetdev);
			goto out;
		}		
		break;
	case Psyskernel:
		if (cnp->cn_flags & CNP_ISDOTDOT) {
			/* XXX: this is wrong, wrong, wrong. */
			error = linprocfs_root(dvp->v_mount, vpp);
			goto out;
		}
		if (CNEQ(cnp, "osrelease", 9)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Posrelease);
			goto out;
		}
		if (CNEQ(cnp, "ostype", 6)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Postype);
			goto out;
		}
		if (CNEQ(cnp, "pid_max", 7)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Ppidmax);
			goto out;
		}
		if (CNEQ(cnp, "version", 7)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Pversion);
			goto out;
		}
		break;
		
	case Proot:
		if (cnp->cn_flags & CNP_ISDOTDOT)
			return (EIO);

		if (CNEQ(cnp, "self", 4)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Pself);
			goto out;
		}
		if (CNEQ(cnp, "meminfo", 7)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Pmeminfo);
			goto out;
		}
		if (CNEQ(cnp, "cpuinfo", 7)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Pcpuinfo);
			goto out;
		}
		if (CNEQ(cnp, "mounts", 6)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Pmounts);
			goto out;
		}
		if (CNEQ(cnp, "stat", 4)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Pstat);
			goto out;
		}
		if (CNEQ(cnp, "uptime", 6)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Puptime);
			goto out;
		}
		if (CNEQ(cnp, "version", 7)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Pversion);
			goto out;
		}
		if (CNEQ(cnp, "loadavg", 7)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Ploadavg);
			goto out;
		}
		if (CNEQ(cnp, "net", 3)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Pnet);
			goto out;
		}
		if (CNEQ(cnp, "sys", 3)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Psys);
			goto out;
		}
		if (CNEQ(cnp, "devices", 7)) {
			error = linprocfs_allocvp(dvp->v_mount, vpp, 0, Pdevices);
			goto out;
		}

		pid = atopid(pname, cnp->cn_namelen);
		if (pid == NO_PID)
			break;

		p = linprocfs_pfind(pid);
		if (p == NULL)
			break;

		if (!PRISON_CHECK(ap->a_cnp->cn_cred, p->p_ucred))
			break;

		if (ps_showallprocs == 0 && ap->a_cnp->cn_cred->cr_uid != 0 &&
		    ap->a_cnp->cn_cred->cr_uid != p->p_ucred->cr_uid)
			break;

		error = linprocfs_allocvp(dvp->v_mount, vpp, pid, Pproc);
		goto out;

	case Pproc:
		if (cnp->cn_flags & CNP_ISDOTDOT) {
			error = linprocfs_root(dvp->v_mount, vpp);
			goto out;
		}

		p = linprocfs_pfind(pfs->pfs_pid);
		if (p == NULL)
			break;

		if (!PRISON_CHECK(ap->a_cnp->cn_cred, p->p_ucred))
			break;

		if (ps_showallprocs == 0 && ap->a_cnp->cn_cred->cr_uid != 0 &&
		    ap->a_cnp->cn_cred->cr_uid != p->p_ucred->cr_uid)
			break;

		for (pt = proc_targets, i = 0; i < nproc_targets; pt++, i++) {
			if (cnp->cn_namelen == pt->pt_namlen &&
			    bcmp(pt->pt_name, pname, cnp->cn_namelen) == 0 &&
			    (pt->pt_valid == NULL || (*pt->pt_valid)(p)))
				goto found;
		}
		break;

	found:
		error = linprocfs_allocvp(dvp->v_mount, vpp, pfs->pfs_pid,
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
	 * dvp was passed to us locked and *vpp must be returned locked
	 * so if dvp != *vpp and CNP_LOCKPARENT is not set, unlock dvp.
	 */
out:
	if (p)
		PRELE(p);
	if (error == 0) {
		if (*vpp != dvp && (cnp->cn_flags & CNP_LOCKPARENT) == 0) {
			cnp->cn_flags |= CNP_PDIRUNLOCK;
			vn_unlock(dvp);
		}
	}
	return (error);
}

/*
 * Does this process have a text file?
 */
int
linprocfs_validfile(struct proc *p)
{

	return (procfs_findtextvp(p) != NULLVP);
}

/*
 * readdir() returns directory entries from pfsnode (vp).
 *
 * We generate just one directory entry at a time, as it would probably
 * not pay off to buffer several entries locally to save uiomove calls.
 *
 * linprocfs_readdir(struct vnode *a_vp, struct uio *a_uio,
 *		     struct ucred *a_cred, int *a_eofflag,
 *		     int *a_ncookies, off_t **a_cookies)
 */
static int
linprocfs_readdir(struct vop_readdir_args *ap)
{
	struct pfsnode *pfs;
	int error;

	if (ap->a_uio->uio_offset < 0 || ap->a_uio->uio_offset > INT_MAX)
		return (EINVAL);

	pfs = VTOPFS(ap->a_vp);
	error = vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY | LK_FAILRECLAIM);
	if (error)
		return (error);

	switch (pfs->pfs_type) {
	case Pproc:
		/*
		 * This is for the process-specific sub-directories.
		 * all that is needed to is copy out all the entries
		 * from the procent[] table (top of this file).
		 */
		error = linprocfs_readdir_proc(ap);
		break;
	case Proot:
		/*
		 * This is for the root of the procfs filesystem
		 * what is needed is a special entry for "self"
		 * followed by an entry for each process on allproc
		 */
		error = linprocfs_readdir_root(ap);
		break;
	case Pnet:
		error = linprocfs_readdir_net(ap);
		break;
	case Psys:
		error = linprocfs_readdir_sys(ap);
		break;
	case Psyskernel:
		error = linprocfs_readdir_syskernel(ap);
		break;
	default:
		error = ENOTDIR;
		break;
	}
	vn_unlock(ap->a_vp);

	return (error);
}

static int
linprocfs_readdir_proc(struct vop_readdir_args *ap)
{
	struct pfsnode *pfs;
	int error, i, retval;
	struct proc *p;
	struct proc_target *pt;
	struct uio *uio = ap->a_uio;

	pfs = VTOPFS(ap->a_vp);
	p = linprocfs_pfind(pfs->pfs_pid);
	if (p == NULL)
		return(0);
	if (!PRISON_CHECK(ap->a_cred, p->p_ucred)) {
		PRELE(p);
		return(0);
	}

	error = 0;
	i = uio->uio_offset;

	for (pt = &proc_targets[i];
	     !error && uio->uio_resid > 0 && i < nproc_targets; pt++, i++) {
		if (pt->pt_valid && (*pt->pt_valid)(p) == 0)
			continue;

		retval = vop_write_dirent(&error, uio,
		    PROCFS_FILENO(pfs->pfs_pid, pt->pt_pfstype), pt->pt_type,
		    pt->pt_namlen, pt->pt_name);
		if (retval)
			break;
	}

	uio->uio_offset = i;
	PRELE(p);

	return(error);
}

struct linprocfs_readdir_root_info {
	int error;
	int pcnt;
	int i;
	struct uio *uio;
	struct ucred *cred;
};

/*
 * Scan the root directory by scanning all process
 */
static int linprocfs_readdir_root_callback(struct proc *p, void *data);

static int
linprocfs_readdir_root(struct vop_readdir_args *ap)
{
	struct linprocfs_readdir_root_info info;
	struct uio *uio = ap->a_uio;
	int res = 0;

	info.error = 0;
	info.i = uio->uio_offset;
	info.pcnt = 0;
	info.uio = uio;
	info.cred = ap->a_cred;

	while (info.pcnt < 13) {
		res = linprocfs_readdir_root_callback(NULL, &info);
		if (res < 0)
			break;
	}
	if (res >= 0)
		allproc_scan(linprocfs_readdir_root_callback, &info);

	uio->uio_offset = info.i;
	return(info.error);
}

static int
linprocfs_readdir_root_callback(struct proc *p, void *data)
{
	struct linprocfs_readdir_root_info *info = data;
	int retval;
	struct uio *uio = info->uio;
	ino_t d_ino;
	const char *d_name;
	char d_name_pid[20];
	size_t d_namlen;
	uint8_t d_type;

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
		d_ino = PROCFS_FILENO(0, Proot);
		d_namlen = 4;
		d_name = "self";
		d_type = DT_LNK;
		break;

	case 3:
		d_ino = PROCFS_FILENO(0, Pmeminfo);
		d_namlen = 7;
		d_name = "meminfo";
		d_type = DT_REG;
		break;

	case 4:
		d_ino = PROCFS_FILENO(0, Pcpuinfo);
		d_namlen = 7;
		d_name = "cpuinfo";
		d_type = DT_REG;
		break;

	case 5:
		d_ino = PROCFS_FILENO(0, Pstat);
		d_namlen = 4;
		d_name = "stat";
		d_type = DT_REG;
		break;
		    
	case 6:
		d_ino = PROCFS_FILENO(0, Puptime);
		d_namlen = 6;
		d_name = "uptime";
		d_type = DT_REG;
		break;

	case 7:
		d_ino = PROCFS_FILENO(0, Pversion);
		d_namlen = 7;
		d_name = "version";
		d_type = DT_REG;
		break;

	case 8:
		d_ino = PROCFS_FILENO(0, Ploadavg);
		d_namlen = 7;
		d_name = "loadavg";
		d_type = DT_REG;
		break;
	case 9:
		d_ino = PROCFS_FILENO(0, Pnet);
		d_namlen = 3;
		d_name = "net";
		d_type = DT_DIR;
		break;
	case 10:
		d_ino = PROCFS_FILENO(0, Psys);
		d_namlen = 3;
		d_name = "sys";
		d_type = DT_DIR;
		break;
	case 11:
		d_ino = PROCFS_FILENO(0, Pmounts);
		d_namlen = 6;
		d_name = "mounts";
		d_type = DT_DIR;
		break;
	case 12:
		d_ino = PROCFS_FILENO(0, Pdevices);
		d_namlen = 7;
		d_name = "devices";
		d_type = DT_REG;
		break;		
	default:
		/*
		 * Ignore processes that aren't in our prison
		 */
		if (PRISON_CHECK(info->cred, p->p_ucred) == 0)
			return(0);

		/*
		 * Ignore processes that we do not want to be visible.
		 */
		if (ps_showallprocs == 0 && 
		    info->cred->cr_uid != 0 &&
		    info->cred->cr_uid != p->p_ucred->cr_uid) {
			return(0);
		}

		/*
		 * Skip processes we have already read (optimization)
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
	 * Skip processes we have already read
	 */
	if (info->pcnt < info->i) {
		++info->pcnt;
		return(0);
	}
	retval = vop_write_dirent(&info->error, info->uio, 
				  d_ino, d_type, d_namlen, d_name);
	if (retval == 0) {
		++info->pcnt;	/* iterate proc candidates scanned */
		++info->i;	/* iterate entries written */
	}
	if (retval || info->error || uio->uio_resid <= 0)
		return(-1);
	return(0);
}

/*
 * Scan the root directory by scanning all process
 */
static int linprocfs_readdir_net_callback(struct proc *p, void *data);

static int
linprocfs_readdir_net(struct vop_readdir_args *ap)
{
	struct linprocfs_readdir_root_info info;
	struct uio *uio = ap->a_uio;
	int res;

	info.error = 0;
	info.i = uio->uio_offset;
	info.pcnt = 0;
	info.uio = uio;
	info.cred = ap->a_cred;

	while (info.pcnt < 3) {
		res = linprocfs_readdir_net_callback(NULL, &info);
		if (res < 0)
			break;
	}

	uio->uio_offset = info.i;
	return(info.error);
}

static int
linprocfs_readdir_net_callback(struct proc *p, void *data)
{
	struct linprocfs_readdir_root_info *info = data;
	int retval;
	struct uio *uio = info->uio;
	ino_t d_ino;
	const char *d_name;
	size_t d_namlen;
	uint8_t d_type;

	switch (info->pcnt) {
	case 0:		/* `.' */
		d_ino = PROCFS_FILENO(0, Pnet);
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
		d_ino = PROCFS_FILENO(0, Pnet);
		d_namlen = 3;
		d_name = "dev";
		d_type = DT_REG;
		break;
	default:
		d_ino = 0;
		d_namlen = 0;
		d_name = NULL;
		d_type = DT_REG;
		break;
	}

	/*
	 * Skip processes we have already read
	 */
	if (info->pcnt < info->i) {
		++info->pcnt;
		return(0);
	}
	retval = vop_write_dirent(&info->error, info->uio, 
				  d_ino, d_type, d_namlen, d_name);
	if (retval == 0) {
		++info->pcnt;	/* iterate proc candidates scanned */
		++info->i;	/* iterate entries written */
	}
	if (retval || info->error || uio->uio_resid <= 0)
		return(-1);
	return(0);
}







/*
 * Scan the root directory by scanning all process
 */
static int linprocfs_readdir_sys_callback(struct proc *p, void *data);

static int
linprocfs_readdir_sys(struct vop_readdir_args *ap)
{
	struct linprocfs_readdir_root_info info;
	struct uio *uio = ap->a_uio;
	int res;

	info.error = 0;
	info.i = uio->uio_offset;
	info.pcnt = 0;
	info.uio = uio;
	info.cred = ap->a_cred;

	while (info.pcnt < 3) {
		res = linprocfs_readdir_sys_callback(NULL, &info);
		if (res < 0)
			break;
	}

	uio->uio_offset = info.i;
	return(info.error);
}

static int
linprocfs_readdir_sys_callback(struct proc *p, void *data)
{
	struct linprocfs_readdir_root_info *info = data;
	int retval;
	struct uio *uio = info->uio;
	ino_t d_ino;
	const char *d_name;
	size_t d_namlen;
	uint8_t d_type;

	switch (info->pcnt) {
	case 0:		/* `.' */
		d_ino = PROCFS_FILENO(0, Psys);
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
		d_ino = PROCFS_FILENO(0, Psyskernel);
		d_namlen = 6;
		d_name = "kernel";
		d_type = DT_DIR;
		break;
	default:
		d_ino = 0;
		d_namlen = 0;
		d_name = NULL;
		d_type = DT_REG;
		break;
	}

	/*
	 * Skip processes we have already read
	 */
	if (info->pcnt < info->i) {
		++info->pcnt;
		return(0);
	}
	retval = vop_write_dirent(&info->error, info->uio, 
				  d_ino, d_type, d_namlen, d_name);
	if (retval == 0) {
		++info->pcnt;	/* iterate proc candidates scanned */
		++info->i;	/* iterate entries written */
	}
	if (retval || info->error || uio->uio_resid <= 0)
		return(-1);
	return(0);
}





/*
 * Scan the root directory by scanning all process
 */
static int linprocfs_readdir_syskernel_callback(struct proc *p, void *data);

static int
linprocfs_readdir_syskernel(struct vop_readdir_args *ap)
{
	struct linprocfs_readdir_root_info info;
	struct uio *uio = ap->a_uio;
	int res;

	info.error = 0;
	info.i = uio->uio_offset;
	info.pcnt = 0;
	info.uio = uio;
	info.cred = ap->a_cred;

	while (info.pcnt < 6) {
		res = linprocfs_readdir_syskernel_callback(NULL, &info);
		if (res < 0)
			break;
	}

	uio->uio_offset = info.i;
	return(info.error);
}

static int
linprocfs_readdir_syskernel_callback(struct proc *p, void *data)
{
	struct linprocfs_readdir_root_info *info = data;
	int retval;
	struct uio *uio = info->uio;
	ino_t d_ino;
	const char *d_name;
	size_t d_namlen;
	uint8_t d_type;

	switch (info->pcnt) {
	case 0:		/* `.' */
		d_ino = PROCFS_FILENO(0, Psyskernel);
		d_name = ".";
		d_namlen = 1;
		d_type = DT_DIR;
		break;
	case 1:		/* `..' */
		d_ino = PROCFS_FILENO(0, Psys);
		d_name = "..";
		d_namlen = 2;
		d_type = DT_DIR;
		break;

	case 2:
		d_ino = PROCFS_FILENO(0, Posrelease);
		d_namlen = 9;
		d_name = "osrelease";
		d_type = DT_REG;
		break;

	case 3:
		d_ino = PROCFS_FILENO(0, Postype);
		d_namlen = 4;
		d_name = "ostype";
		d_type = DT_REG;
		break;

	case 4:
		d_ino = PROCFS_FILENO(0, Pversion);
		d_namlen = 7;
		d_name = "version";
		d_type = DT_REG;
		break;

	case 5:
		d_ino = PROCFS_FILENO(0, Ppidmax);
		d_namlen = 7;
		d_name = "pid_max";
		d_type = DT_REG;
		break;
	default:
		d_ino = 0;
		d_namlen = 0;
		d_name = NULL;
		d_type = DT_REG;
		break;
	}

	/*
	 * Skip processes we have already read
	 */
	if (info->pcnt < info->i) {
		++info->pcnt;
		return(0);
	}
	retval = vop_write_dirent(&info->error, info->uio, 
				  d_ino, d_type, d_namlen, d_name);
	if (retval == 0) {
		++info->pcnt;	/* iterate proc candidates scanned */
		++info->i;	/* iterate entries written */
	}
	if (retval || info->error || uio->uio_resid <= 0)
		return(-1);
	return(0);
}

/*
 * readlink reads the link of `self' or `exe'
 */
static int
linprocfs_readlink(struct vop_readlink_args *ap)
{
	char buf[16];		/* should be enough */
	struct proc *procp;
	struct vnode *vp = ap->a_vp;
	struct nchandle *nchp;
	struct pfsnode *pfs = VTOPFS(vp);
	char *fullpath, *freepath;
	int error, len;

	error = 0;
	procp = NULL;

	switch (pfs->pfs_type) {
	case Pself:
		if (pfs->pfs_fileno != PROCFS_FILENO(0, Pself))
			return (EINVAL);

		len = ksnprintf(buf, sizeof(buf), "%ld", (long)curproc->p_pid);

		error = uiomove(buf, len, ap->a_uio);
		break;
	/*
	 * There _should_ be no way for an entire process to disappear
	 * from under us...
	 */
	case Pexe:
		procp = linprocfs_pfind(pfs->pfs_pid);
		if (procp == NULL || procp->p_ucred == NULL) {
			kprintf("linprocfs_readlink: pid %d disappeared\n",
			    pfs->pfs_pid);
			error = uiomove("unknown", sizeof("unknown") - 1,
					ap->a_uio);
			break;
		}
		error = cache_fullpath(procp, &procp->p_textnch, NULL,
				       &fullpath, &freepath, 0);
		if (error != 0) {
			error = uiomove("unknown", sizeof("unknown") - 1,
					ap->a_uio);
			break;
		}
		error = uiomove(fullpath, strlen(fullpath), ap->a_uio);
		kfree(freepath, M_TEMP);
		break;
	case Pcwd:
		procp = linprocfs_pfind(pfs->pfs_pid);
		if (procp == NULL || procp->p_ucred == NULL) {
			kprintf("linprocfs_readlink: pid %d disappeared\n",
				pfs->pfs_pid);
			error = uiomove("unknown", sizeof("unknown") - 1,
					ap->a_uio);
			break;
		}
		error = cache_fullpath(procp, &procp->p_fd->fd_ncdir, NULL,
				       &fullpath, &freepath, 0);
		if (error != 0) {
			error = uiomove("unknown", sizeof("unknown") - 1,
					ap->a_uio);
			break;
		}
		error = uiomove(fullpath, strlen(fullpath), ap->a_uio);
		kfree(freepath, M_TEMP);
		break;
	case Pprocroot:
		procp = linprocfs_pfind(pfs->pfs_pid);
		if (procp == NULL || procp->p_ucred == NULL) {
			kprintf("linprocfs_readlink: pid %d disappeared\n",
			    pfs->pfs_pid);
			error = uiomove("unknown", sizeof("unknown") - 1,
					ap->a_uio);
			break;
		}
		nchp = jailed(procp->p_ucred) ? &procp->p_fd->fd_njdir : &procp->p_fd->fd_nrdir;
		error = cache_fullpath(procp, nchp, NULL,
				       &fullpath, &freepath, 0);
		if (error != 0) {
			error = uiomove("unknown", sizeof("unknown") - 1,
					ap->a_uio);
			break;
		}
		error = uiomove(fullpath, strlen(fullpath), ap->a_uio);
		kfree(freepath, M_TEMP);
		break;
	case Pfd:
		procp = linprocfs_pfind(pfs->pfs_pid);
		if (procp == NULL || procp->p_ucred == NULL) {
			kprintf("linprocfs_readlink: pid %d disappeared\n",
			    pfs->pfs_pid);
			error = uiomove("unknown", sizeof("unknown") - 1,
					ap->a_uio);
			break;
		}
		if (procp == curproc) {
			error = uiomove("/dev/fd", sizeof("/dev/fd") - 1,
					ap->a_uio);
			break;
		} else {
			error = uiomove("unknown", sizeof("unknown") - 1,
					ap->a_uio);
			break;
		}
		/* notreached */
		break;
	default:
		error = EINVAL;
		break;
	}
	if (procp)
		PRELE(procp);
	return error;
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

