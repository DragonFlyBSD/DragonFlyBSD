/*
 * Copyright (c) 1989, 1993
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
 *	@(#)vfs_syscalls.c	8.13 (Berkeley) 4/15/94
 * $FreeBSD: src/sys/kern/vfs_syscalls.c,v 1.151.2.18 2003/04/04 20:35:58 tegge Exp $
 * $DragonFly: src/sys/kern/vfs_syscalls.c,v 1.50 2004/12/24 05:00:17 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/sysent.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/sysproto.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/linker.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/dirent.h>
#include <sys/extattr.h>
#include <sys/kern_syscall.h>

#include <machine/limits.h>
#include <vfs/union/union.h>
#include <sys/sysctl.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_zone.h>
#include <vm/vm_page.h>

#include <sys/file2.h>

static int checkvp_chdir (struct vnode *vn, struct thread *td);
static void checkdirs (struct vnode *olddp, struct namecache *ncp);
static int chroot_refuse_vdir_fds (struct filedesc *fdp);
static int getutimes (const struct timeval *, struct timespec *);
static int setfown (struct vnode *, uid_t, gid_t);
static int setfmode (struct vnode *, int);
static int setfflags (struct vnode *, int);
static int setutimes (struct vnode *, const struct timespec *, int);
static int	usermount = 0;	/* if 1, non-root can mount fs. */

int (*union_dircheckp) (struct thread *, struct vnode **, struct file *);

SYSCTL_INT(_vfs, OID_AUTO, usermount, CTLFLAG_RW, &usermount, 0, "");

/*
 * Virtual File System System Calls
 */

/*
 * Mount a file system.
 */
/*
 * mount_args(char *type, char *path, int flags, caddr_t data)
 */
/* ARGSUSED */
int
mount(struct mount_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct namecache *ncp;
	struct mount *mp;
	struct vfsconf *vfsp;
	int error, flag = 0, flag2 = 0;
	struct vattr va;
	struct nlookupdata nd;
	char fstypename[MFSNAMELEN];
	lwkt_tokref ilock;
	struct nlcomponent nlc;

	KKASSERT(p);
	if (p->p_ucred->cr_prison != NULL)
		return (EPERM);
	if (usermount == 0 && (error = suser(td)))
		return (error);
	/*
	 * Do not allow NFS export by non-root users.
	 */
	if (SCARG(uap, flags) & MNT_EXPORTED) {
		error = suser(td);
		if (error)
			return (error);
	}
	/*
	 * Silently enforce MNT_NOSUID and MNT_NODEV for non-root users
	 */
	if (suser(td)) 
		SCARG(uap, flags) |= MNT_NOSUID | MNT_NODEV;

	/*
	 * Lookup the requested path and extract the ncp and vnode.
	 */
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0) {
		if ((error = nlookup(&nd)) == 0) {
			if (nd.nl_ncp->nc_vp == NULL)
				error = ENOENT;
		}
	}
	if (error) {
		nlookup_done(&nd);
		return (error);
	}

	/*
	 * Extract the locked+refd ncp and cleanup the nd structure
	 */
	ncp = nd.nl_ncp;
	nd.nl_ncp = NULL;
	nlookup_done(&nd);

	/*
	 * now we have the locked ref'd ncp and unreferenced vnode.
	 */
	vp = ncp->nc_vp;
	if ((error = vget(vp, LK_EXCLUSIVE, td)) != 0) {
		cache_put(ncp);
		return (error);
	}
	cache_unlock(ncp);

	/*
	 * Now we have an unlocked ref'd ncp and a locked ref'd vp
	 */
	if (SCARG(uap, flags) & MNT_UPDATE) {
		if ((vp->v_flag & VROOT) == 0) {
			cache_drop(ncp);
			vput(vp);
			return (EINVAL);
		}
		mp = vp->v_mount;
		flag = mp->mnt_flag;
		flag2 = mp->mnt_kern_flag;
		/*
		 * We only allow the filesystem to be reloaded if it
		 * is currently mounted read-only.
		 */
		if ((SCARG(uap, flags) & MNT_RELOAD) &&
		    ((mp->mnt_flag & MNT_RDONLY) == 0)) {
			cache_drop(ncp);
			vput(vp);
			return (EOPNOTSUPP);	/* Needs translation */
		}
		/*
		 * Only root, or the user that did the original mount is
		 * permitted to update it.
		 */
		if (mp->mnt_stat.f_owner != p->p_ucred->cr_uid &&
		    (error = suser(td))) {
			cache_drop(ncp);
			vput(vp);
			return (error);
		}
		if (vfs_busy(mp, LK_NOWAIT, NULL, td)) {
			cache_drop(ncp);
			vput(vp);
			return (EBUSY);
		}
		if ((vp->v_flag & VMOUNT) != 0 ||
		    vp->v_mountedhere != NULL) {
			cache_drop(ncp);
			vfs_unbusy(mp, td);
			vput(vp);
			return (EBUSY);
		}
		vp->v_flag |= VMOUNT;
		mp->mnt_flag |=
		    SCARG(uap, flags) & (MNT_RELOAD | MNT_FORCE | MNT_UPDATE);
		VOP_UNLOCK(vp, 0, td);
		goto update;
	}
	/*
	 * If the user is not root, ensure that they own the directory
	 * onto which we are attempting to mount.
	 */
	if ((error = VOP_GETATTR(vp, &va, td)) ||
	    (va.va_uid != p->p_ucred->cr_uid &&
	     (error = suser(td)))) {
		cache_drop(ncp);
		vput(vp);
		return (error);
	}
	if ((error = vinvalbuf(vp, V_SAVE, td, 0, 0)) != 0) {
		cache_drop(ncp);
		vput(vp);
		return (error);
	}
	if (vp->v_type != VDIR) {
		cache_drop(ncp);
		vput(vp);
		return (ENOTDIR);
	}
	if ((error = copyinstr(SCARG(uap, type), fstypename, MFSNAMELEN, NULL)) != 0) {
		cache_drop(ncp);
		vput(vp);
		return (error);
	}
	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next) {
		if (!strcmp(vfsp->vfc_name, fstypename))
			break;
	}
	if (vfsp == NULL) {
		linker_file_t lf;

		/* Only load modules for root (very important!) */
		if ((error = suser(td)) != 0) {
			cache_drop(ncp);
			vput(vp);
			return error;
		}
		error = linker_load_file(fstypename, &lf);
		if (error || lf == NULL) {
			cache_drop(ncp);
			vput(vp);
			if (lf == NULL)
				error = ENODEV;
			return error;
		}
		lf->userrefs++;
		/* lookup again, see if the VFS was loaded */
		for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next) {
			if (!strcmp(vfsp->vfc_name, fstypename))
				break;
		}
		if (vfsp == NULL) {
			lf->userrefs--;
			linker_file_unload(lf);
			cache_drop(ncp);
			vput(vp);
			return (ENODEV);
		}
	}
	if ((vp->v_flag & VMOUNT) != 0 ||
	    vp->v_mountedhere != NULL) {
		cache_drop(ncp);
		vput(vp);
		return (EBUSY);
	}
	vp->v_flag |= VMOUNT;

	/*
	 * Allocate and initialize the filesystem.
	 */
	mp = malloc(sizeof(struct mount), M_MOUNT, M_ZERO|M_WAITOK);
	TAILQ_INIT(&mp->mnt_nvnodelist);
	TAILQ_INIT(&mp->mnt_reservedvnlist);
	mp->mnt_nvnodelistsize = 0;
	lockinit(&mp->mnt_lock, 0, "vfslock", 0, LK_NOPAUSE);
	vfs_busy(mp, LK_NOWAIT, NULL, td);
	mp->mnt_op = vfsp->vfc_vfsops;
	mp->mnt_vfc = vfsp;
	vfsp->vfc_refcount++;
	mp->mnt_stat.f_type = vfsp->vfc_typenum;
	mp->mnt_flag |= vfsp->vfc_flags & MNT_VISFLAGMASK;
	strncpy(mp->mnt_stat.f_fstypename, vfsp->vfc_name, MFSNAMELEN);
	mp->mnt_vnodecovered = vp;
	mp->mnt_stat.f_owner = p->p_ucred->cr_uid;
	mp->mnt_iosize_max = DFLTPHYS;
	VOP_UNLOCK(vp, 0, td);
update:
	/*
	 * Set the mount level flags.
	 */
	if (SCARG(uap, flags) & MNT_RDONLY)
		mp->mnt_flag |= MNT_RDONLY;
	else if (mp->mnt_flag & MNT_RDONLY)
		mp->mnt_kern_flag |= MNTK_WANTRDWR;
	mp->mnt_flag &=~ (MNT_NOSUID | MNT_NOEXEC | MNT_NODEV |
	    MNT_SYNCHRONOUS | MNT_UNION | MNT_ASYNC | MNT_NOATIME |
	    MNT_NOSYMFOLLOW | MNT_IGNORE |
	    MNT_NOCLUSTERR | MNT_NOCLUSTERW | MNT_SUIDDIR);
	mp->mnt_flag |= SCARG(uap, flags) & (MNT_NOSUID | MNT_NOEXEC |
	    MNT_NODEV | MNT_SYNCHRONOUS | MNT_UNION | MNT_ASYNC | MNT_FORCE |
	    MNT_NOSYMFOLLOW | MNT_IGNORE |
	    MNT_NOATIME | MNT_NOCLUSTERR | MNT_NOCLUSTERW | MNT_SUIDDIR);
	/*
	 * Mount the filesystem.
	 * XXX The final recipients of VFS_MOUNT just overwrite the ndp they
	 * get. 
	 */
	error = VFS_MOUNT(mp, SCARG(uap, path), SCARG(uap, data), td);
	if (mp->mnt_flag & MNT_UPDATE) {
		if (mp->mnt_kern_flag & MNTK_WANTRDWR)
			mp->mnt_flag &= ~MNT_RDONLY;
		mp->mnt_flag &=~ (MNT_UPDATE | MNT_RELOAD | MNT_FORCE);
		mp->mnt_kern_flag &=~ MNTK_WANTRDWR;
		if (error) {
			mp->mnt_flag = flag;
			mp->mnt_kern_flag = flag2;
		}
		vfs_unbusy(mp, td);
		vp->v_flag &= ~VMOUNT;
		vrele(vp);
		cache_drop(ncp);
		return (error);
	}
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	/*
	 * Put the new filesystem on the mount list after root.  The mount
	 * point gets its own mnt_ncp which is a special ncp linking the
	 * vnode-under to the root of the new mount.  The lookup code
	 * detects the mount point going forward and detects the special
	 * mnt_ncp via NCP_MOUNTPT going backwards.
	 *
	 * It is not necessary to invalidate or purge the vnode underneath
	 * because elements under the mount will be given their own glue
	 * namecache record.
	 */
	if (!error) {
		nlc.nlc_nameptr = "";
		nlc.nlc_namelen = 0;
		mp->mnt_ncp = cache_nlookup(ncp, &nlc);
		mp->mnt_ncp->nc_flag |= NCF_MOUNTPT;
		mp->mnt_ncp->nc_mount = mp;
		cache_drop(ncp);
		/* XXX get the root of the fs and cache_setvp(mnt_ncp...) */
		vp->v_flag &= ~VMOUNT;
		vp->v_mountedhere = mp;
		lwkt_gettoken(&ilock, &mountlist_token);
		TAILQ_INSERT_TAIL(&mountlist, mp, mnt_list);
		lwkt_reltoken(&ilock);
		checkdirs(vp, mp->mnt_ncp);
		cache_unlock(mp->mnt_ncp);	/* leave ref intact */
		VOP_UNLOCK(vp, 0, td);
		error = vfs_allocate_syncvnode(mp);
		vfs_unbusy(mp, td);
		if ((error = VFS_START(mp, 0, td)) != 0)
			vrele(vp);
	} else {
		vfs_rm_vnodeops(&mp->mnt_vn_coherency_ops);
		vfs_rm_vnodeops(&mp->mnt_vn_journal_ops);
		vfs_rm_vnodeops(&mp->mnt_vn_norm_ops);
		vfs_rm_vnodeops(&mp->mnt_vn_spec_ops);
		vfs_rm_vnodeops(&mp->mnt_vn_fifo_ops);
		vp->v_flag &= ~VMOUNT;
		mp->mnt_vfc->vfc_refcount--;
		vfs_unbusy(mp, td);
		free(mp, M_MOUNT);
		cache_drop(ncp);
		vput(vp);
	}
	return (error);
}

/*
 * Scan all active processes to see if any of them have a current
 * or root directory onto which the new filesystem has just been
 * mounted. If so, replace them with the new mount point.
 *
 * The passed ncp is ref'd and locked (from the mount code) and
 * must be associated with the vnode representing the root of the
 * mount point.
 */
static void
checkdirs(struct vnode *olddp, struct namecache *ncp)
{
	struct filedesc *fdp;
	struct vnode *newdp;
	struct mount *mp;
	struct proc *p;

	if (olddp->v_usecount == 1)
		return;
	mp = olddp->v_mountedhere;
	if (VFS_ROOT(mp, &newdp))
		panic("mount: lost mount");
	cache_setvp(ncp, newdp);

	if (rootvnode == olddp) {
		vref(newdp);
		vfs_cache_setroot(newdp, cache_hold(ncp));
	}

	FOREACH_PROC_IN_SYSTEM(p) {
		fdp = p->p_fd;
		if (fdp->fd_cdir == olddp) {
			vrele(fdp->fd_cdir);
			vref(newdp);
			fdp->fd_cdir = newdp;
			cache_drop(fdp->fd_ncdir);
			fdp->fd_ncdir = cache_hold(ncp);
		}
		if (fdp->fd_rdir == olddp) {
			vrele(fdp->fd_rdir);
			vref(newdp);
			fdp->fd_rdir = newdp;
			cache_drop(fdp->fd_nrdir);
			fdp->fd_nrdir = cache_hold(ncp);
		}
	}
	vput(newdp);
}

/*
 * Unmount a file system.
 *
 * Note: unmount takes a path to the vnode mounted on as argument,
 * not special file (as before).
 */
/*
 * umount_args(char *path, int flags)
 */
/* ARGSUSED */
int
unmount(struct unmount_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct mount *mp;
	int error;
	struct nlookupdata nd;

	KKASSERT(p);
	if (p->p_ucred->cr_prison != NULL)
		return (EPERM);
	if (usermount == 0 && (error = suser(td)))
		return (error);

	vp = NULL;
	error = nlookup_init(&nd, SCARG(uap, path), UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(nd.nl_ncp, nd.nl_cred, LK_EXCLUSIVE, &vp);
	nlookup_done(&nd);
	if (error)
		return (error);

	mp = vp->v_mount;

	/*
	 * Only root, or the user that did the original mount is
	 * permitted to unmount this filesystem.
	 */
	if ((mp->mnt_stat.f_owner != p->p_ucred->cr_uid) &&
	    (error = suser(td))) {
		vput(vp);
		return (error);
	}

	/*
	 * Don't allow unmounting the root file system.
	 */
	if (mp->mnt_flag & MNT_ROOTFS) {
		vput(vp);
		return (EINVAL);
	}

	/*
	 * Must be the root of the filesystem
	 */
	if ((vp->v_flag & VROOT) == 0) {
		vput(vp);
		return (EINVAL);
	}
	vput(vp);
	return (dounmount(mp, SCARG(uap, flags), td));
}

/*
 * Do the actual file system unmount.
 */
int
dounmount(struct mount *mp, int flags, struct thread *td)
{
	struct vnode *coveredvp;
	int error;
	int async_flag;
	lwkt_tokref ilock;

	lwkt_gettoken(&ilock, &mountlist_token);
	if (mp->mnt_kern_flag & MNTK_UNMOUNT) {
		lwkt_reltoken(&ilock);
		return (EBUSY);
	}
	mp->mnt_kern_flag |= MNTK_UNMOUNT;
	/* Allow filesystems to detect that a forced unmount is in progress. */
	if (flags & MNT_FORCE)
		mp->mnt_kern_flag |= MNTK_UNMOUNTF;
	error = lockmgr(&mp->mnt_lock, LK_DRAIN | LK_INTERLOCK |
	    ((flags & MNT_FORCE) ? 0 : LK_NOWAIT), &ilock, td);
	if (error) {
		mp->mnt_kern_flag &= ~(MNTK_UNMOUNT | MNTK_UNMOUNTF);
		if (mp->mnt_kern_flag & MNTK_MWAIT)
			wakeup(mp);
		return (error);
	}

	if (mp->mnt_flag & MNT_EXPUBLIC)
		vfs_setpublicfs(NULL, NULL, NULL);

	vfs_msync(mp, MNT_WAIT);
	async_flag = mp->mnt_flag & MNT_ASYNC;
	mp->mnt_flag &=~ MNT_ASYNC;
	cache_purgevfs(mp);	/* remove cache entries for this file sys */
	if (mp->mnt_syncer != NULL)
		vrele(mp->mnt_syncer);
	if (((mp->mnt_flag & MNT_RDONLY) ||
	     (error = VFS_SYNC(mp, MNT_WAIT, td)) == 0) ||
	    (flags & MNT_FORCE))
		error = VFS_UNMOUNT(mp, flags, td);
	lwkt_gettokref(&ilock);
	if (error) {
		if (mp->mnt_syncer == NULL)
			vfs_allocate_syncvnode(mp);
		mp->mnt_kern_flag &= ~(MNTK_UNMOUNT | MNTK_UNMOUNTF);
		mp->mnt_flag |= async_flag;
		lockmgr(&mp->mnt_lock, LK_RELEASE | LK_INTERLOCK | LK_REENABLE,
		    &ilock, td);
		if (mp->mnt_kern_flag & MNTK_MWAIT)
			wakeup(mp);
		return (error);
	}
	TAILQ_REMOVE(&mountlist, mp, mnt_list);

	/*
	 * Remove any installed vnode ops here so the individual VFSs don't
	 * have to.
	 */
	vfs_rm_vnodeops(&mp->mnt_vn_coherency_ops);
	vfs_rm_vnodeops(&mp->mnt_vn_journal_ops);
	vfs_rm_vnodeops(&mp->mnt_vn_norm_ops);
	vfs_rm_vnodeops(&mp->mnt_vn_spec_ops);
	vfs_rm_vnodeops(&mp->mnt_vn_fifo_ops);

	if ((coveredvp = mp->mnt_vnodecovered) != NULLVP) {
		coveredvp->v_mountedhere = NULL;
		vrele(coveredvp);
		cache_drop(mp->mnt_ncp);
		mp->mnt_ncp = NULL;
	}
	mp->mnt_vfc->vfc_refcount--;
	if (!TAILQ_EMPTY(&mp->mnt_nvnodelist))
		panic("unmount: dangling vnode");
	lockmgr(&mp->mnt_lock, LK_RELEASE | LK_INTERLOCK, &ilock, td);
	if (mp->mnt_kern_flag & MNTK_MWAIT)
		wakeup(mp);
	free(mp, M_MOUNT);
	return (0);
}

/*
 * Sync each mounted filesystem.
 */

#ifdef DEBUG
static int syncprt = 0;
SYSCTL_INT(_debug, OID_AUTO, syncprt, CTLFLAG_RW, &syncprt, 0, "");
#endif /* DEBUG */

/* ARGSUSED */
int
sync(struct sync_args *uap)
{
	struct thread *td = curthread;
	struct mount *mp, *nmp;
	lwkt_tokref ilock;
	int asyncflag;

	lwkt_gettoken(&ilock, &mountlist_token);
	for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &ilock, td)) {
			nmp = TAILQ_NEXT(mp, mnt_list);
			continue;
		}
		if ((mp->mnt_flag & MNT_RDONLY) == 0) {
			asyncflag = mp->mnt_flag & MNT_ASYNC;
			mp->mnt_flag &= ~MNT_ASYNC;
			vfs_msync(mp, MNT_NOWAIT);
			VFS_SYNC(mp, MNT_NOWAIT, td);
			mp->mnt_flag |= asyncflag;
		}
		lwkt_gettokref(&ilock);
		nmp = TAILQ_NEXT(mp, mnt_list);
		vfs_unbusy(mp, td);
	}
	lwkt_reltoken(&ilock);
/*
 * print out buffer pool stat information on each sync() call.
 */
#ifdef DEBUG
	if (syncprt)
		vfs_bufstats();
#endif /* DEBUG */
	return (0);
}

/* XXX PRISON: could be per prison flag */
static int prison_quotas;
#if 0
SYSCTL_INT(_kern_prison, OID_AUTO, quotas, CTLFLAG_RW, &prison_quotas, 0, "");
#endif

/*
 *  quotactl_args(char *path, int fcmd, int uid, caddr_t arg)
 *
 * Change filesystem quotas.
 */
/* ARGSUSED */
int
quotactl(struct quotactl_args *uap)
{
	struct nlookupdata nd;
	struct thread *td;
	struct proc *p;
	struct mount *mp;
	int error;

	td = curthread;
	p = td->td_proc;
	if (p->p_ucred->cr_prison && !prison_quotas)
		return (EPERM);

	error = nlookup_init(&nd, SCARG(uap, path), UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0) {
		mp = nd.nl_ncp->nc_mount;
		error = VFS_QUOTACTL(mp, SCARG(uap, cmd), SCARG(uap, uid),
				    SCARG(uap, arg), nd.nl_td);
	}
	nlookup_done(&nd);
	return (error);
}

/*
 * mountctl(char *path, int op, const void *ctl, int ctllen,
 *		void *buf, int buflen)
 *
 * This function operates on a mount point and executes the specified
 * operation using the specified control data, and possibly returns data.
 *
 * The actual number of bytes stored in the result buffer is returned, 0
 * if none, otherwise an error is returned.
 */
/* ARGSUSED */
int
mountctl(struct mountctl_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	void *ctl = NULL;
	void *buf = NULL;
	char *path = NULL;
	int error;

	/*
	 * Sanity and permissions checks.  We must be root.
	 */
	KKASSERT(p);
	if (p->p_ucred->cr_prison != NULL)
		return (EPERM);
	if ((error = suser(td)) != 0)
		return (error);

	/*
	 * Argument length checks
	 */
	if (uap->ctllen < 0 || uap->ctllen > MAXPATHLEN)
		return (EINVAL);
	if (uap->buflen < 0 || uap->buflen > MAXPATHLEN)
		return (EINVAL);
	if (uap->path == NULL)
		return (EINVAL);

	/*
	 * Allocate the necessary buffers and copyin data
	 */
	path = zalloc(namei_zone);
	error = copyinstr(uap->path, path, MAXPATHLEN, NULL);
	if (error)
		goto done;

	if (uap->ctllen) {
		ctl = malloc(uap->ctllen + 1, M_TEMP, M_WAITOK|M_ZERO);
		error = copyin(uap->ctl, ctl, uap->ctllen);
		if (error)
			goto done;
	}
	if (uap->buflen)
		buf = malloc(uap->buflen + 1, M_TEMP, M_WAITOK|M_ZERO);

	/*
	 * Execute the internal kernel function and clean up.
	 */
	error = kern_mountctl(path, uap->op, ctl, uap->ctllen, buf, uap->buflen, &uap->sysmsg_result);
	if (error == 0 && uap->sysmsg_result > 0)
		error = copyout(buf, uap->buf, uap->sysmsg_result);
done:
	if (path)
		zfree(namei_zone, path);
	if (ctl)
		free(ctl, M_TEMP);
	if (buf)
		free(buf, M_TEMP);
	return (error);
}

/*
 * Execute a mount control operation by resolving the path to a mount point
 * and calling vop_mountctl().  
 */
int
kern_mountctl(const char *path, int op, const void *ctl, int ctllen, 
		void *buf, int buflen, int *res)
{
	struct thread *td = curthread;
	struct vnode *vp;
	struct mount *mp;
	struct nlookupdata nd;
	int error;

	*res = 0;
	vp = NULL;
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(nd.nl_ncp, nd.nl_cred, LK_EXCLUSIVE, &vp);
	nlookup_done(&nd);
	if (error)
		return (error);

	mp = vp->v_mount;

	/*
	 * Must be the root of the filesystem
	 */
	if ((vp->v_flag & VROOT) == 0) {
		vput(vp);
		return (EINVAL);
	}
	error = vop_mountctl(mp->mnt_vn_use_ops, op, ctl, ctllen, 
				buf, buflen, res);
	vput(vp);
	return (error);
}

int
kern_statfs(struct nlookupdata *nd, struct statfs *buf)
{
	struct thread *td = curthread;
	struct mount *mp;
	struct statfs *sp;
	int error;

	if ((error = nlookup(nd)) != 0)
		return (error);
	mp = nd->nl_ncp->nc_mount;
	sp = &mp->mnt_stat;
	if ((error = VFS_STATFS(mp, sp, td)) != 0)
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	bcopy(sp, buf, sizeof(*buf));
	/* Only root should have access to the fsid's. */
	if (suser(td))
		buf->f_fsid.val[0] = buf->f_fsid.val[1] = 0;
	return (0);
}

/*
 * statfs_args(char *path, struct statfs *buf)
 *
 * Get filesystem statistics.
 */
int
statfs(struct statfs_args *uap)
{
	struct nlookupdata nd;
	struct statfs buf;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_statfs(&nd, &buf);
	nlookup_done(&nd);
	if (error == 0)
		error = copyout(&buf, uap->buf, sizeof(*uap->buf));
	return (error);
}

int
kern_fstatfs(int fd, struct statfs *buf)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct mount *mp;
	struct statfs *sp;
	int error;

	KKASSERT(p);
	error = getvnode(p->p_fd, fd, &fp);
	if (error)
		return (error);
	mp = ((struct vnode *)fp->f_data)->v_mount;
	if (mp == NULL)
		return (EBADF);
	sp = &mp->mnt_stat;
	error = VFS_STATFS(mp, sp, td);
	if (error)
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	bcopy(sp, buf, sizeof(*buf));
	/* Only root should have access to the fsid's. */
	if (suser(td))
		buf->f_fsid.val[0] = buf->f_fsid.val[1] = 0;
	return (0);
}

/*
 * fstatfs_args(int fd, struct statfs *buf)
 *
 * Get filesystem statistics.
 */
int
fstatfs(struct fstatfs_args *uap)
{
	struct statfs buf;
	int error;

	error = kern_fstatfs(uap->fd, &buf);

	if (error == 0)
		error = copyout(&buf, uap->buf, sizeof(*uap->buf));
	return (error);
}

/*
 * getfsstat_args(struct statfs *buf, long bufsize, int flags)
 *
 * Get statistics on all filesystems.
 */
/* ARGSUSED */
int
getfsstat(struct getfsstat_args *uap)
{
	struct thread *td = curthread;
	struct mount *mp, *nmp;
	struct statfs *sp;
	caddr_t sfsp;
	lwkt_tokref ilock;
	long count, maxcount, error;

	maxcount = SCARG(uap, bufsize) / sizeof(struct statfs);
	sfsp = (caddr_t)SCARG(uap, buf);
	count = 0;
	lwkt_gettoken(&ilock, &mountlist_token);
	for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &ilock, td)) {
			nmp = TAILQ_NEXT(mp, mnt_list);
			continue;
		}
		if (sfsp && count < maxcount) {
			sp = &mp->mnt_stat;
			/*
			 * If MNT_NOWAIT or MNT_LAZY is specified, do not
			 * refresh the fsstat cache. MNT_NOWAIT or MNT_LAZY
			 * overrides MNT_WAIT.
			 */
			if (((SCARG(uap, flags) & (MNT_LAZY|MNT_NOWAIT)) == 0 ||
			    (SCARG(uap, flags) & MNT_WAIT)) &&
			    (error = VFS_STATFS(mp, sp, td))) {
				lwkt_gettokref(&ilock);
				nmp = TAILQ_NEXT(mp, mnt_list);
				vfs_unbusy(mp, td);
				continue;
			}
			sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
			error = copyout(sp, sfsp, sizeof(*sp));
			if (error) {
				vfs_unbusy(mp, td);
				return (error);
			}
			sfsp += sizeof(*sp);
		}
		count++;
		lwkt_gettokref(&ilock);
		nmp = TAILQ_NEXT(mp, mnt_list);
		vfs_unbusy(mp, td);
	}
	lwkt_reltoken(&ilock);
	if (sfsp && count > maxcount)
		uap->sysmsg_result = maxcount;
	else
		uap->sysmsg_result = count;
	return (0);
}

/*
 * fchdir_args(int fd)
 *
 * Change current working directory to a given file descriptor.
 */
/* ARGSUSED */
int
fchdir(struct fchdir_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct vnode *vp, *ovp;
	struct mount *mp;
	struct file *fp;
	struct namecache *ncp, *oncp;
	struct namecache *nct;
	int error;

	if ((error = getvnode(fdp, SCARG(uap, fd), &fp)) != 0)
		return (error);
	vp = (struct vnode *)fp->f_data;
	vref(vp);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	if (vp->v_type != VDIR || fp->f_ncp == NULL)
		error = ENOTDIR;
	else
		error = VOP_ACCESS(vp, VEXEC, p->p_ucred, td);
	if (error) {
		vput(vp);
		return (error);
	}
	ncp = cache_hold(fp->f_ncp);
	while (!error && (mp = vp->v_mountedhere) != NULL) {
		error = nlookup_mp(mp, &nct);
		if (error == 0) {
			cache_unlock(nct);	/* leave ref intact */
			vput(vp);
			vp = nct->nc_vp;
			error = vget(vp, LK_SHARED, td);
			KKASSERT(error == 0);
			cache_drop(ncp);
			ncp = nct;
		}
	}
	if (error == 0) {
		ovp = fdp->fd_cdir;
		oncp = fdp->fd_ncdir;
		VOP_UNLOCK(vp, 0, td);	/* leave ref intact */
		fdp->fd_cdir = vp;
		fdp->fd_ncdir = ncp;
		cache_drop(oncp);
		vrele(ovp);
	} else {
		cache_drop(ncp);
		vput(vp);
	}
	return (error);
}

int
kern_chdir(struct nlookupdata *nd)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct vnode *vp, *ovp;
	struct namecache *oncp;
	int error;

	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((vp = nd->nl_ncp->nc_vp) == NULL)
		return (ENOENT);
	if ((error = vget(vp, LK_SHARED, td)) != 0)
		return (error);

	error = checkvp_chdir(vp, td);
	VOP_UNLOCK(vp, 0, td);
	if (error == 0) {
		ovp = fdp->fd_cdir;
		oncp = fdp->fd_ncdir;
		cache_unlock(nd->nl_ncp);	/* leave reference intact */
		fdp->fd_ncdir = nd->nl_ncp;
		fdp->fd_cdir = vp;
		cache_drop(oncp);
		vrele(ovp);
		nd->nl_ncp = NULL;
	} else {
		vrele(vp);
	}
	return (error);
}

/*
 * chdir_args(char *path)
 *
 * Change current working directory (``.'').
 */
int
chdir(struct chdir_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_chdir(&nd);
	nlookup_done(&nd);
	return (error);
}

/*
 * Helper function for raised chroot(2) security function:  Refuse if
 * any filedescriptors are open directories.
 */
static int
chroot_refuse_vdir_fds(fdp)
	struct filedesc *fdp;
{
	struct vnode *vp;
	struct file *fp;
	int error;
	int fd;

	for (fd = 0; fd < fdp->fd_nfiles ; fd++) {
		error = getvnode(fdp, fd, &fp);
		if (error)
			continue;
		vp = (struct vnode *)fp->f_data;
		if (vp->v_type != VDIR)
			continue;
		return(EPERM);
	}
	return (0);
}

/*
 * This sysctl determines if we will allow a process to chroot(2) if it
 * has a directory open:
 *	0: disallowed for all processes.
 *	1: allowed for processes that were not already chroot(2)'ed.
 *	2: allowed for all processes.
 */

static int chroot_allow_open_directories = 1;

SYSCTL_INT(_kern, OID_AUTO, chroot_allow_open_directories, CTLFLAG_RW,
     &chroot_allow_open_directories, 0, "");

/*
 * chroot to the specified namecache entry.  We obtain the vp from the
 * namecache data.  The passed ncp must be locked and referenced and will
 * remain locked and referenced on return.
 */
static int
kern_chroot(struct nlookupdata *nd)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct namecache *ncp;
	struct vnode *vp;
	int error;

	/*
	 * Only root can chroot
	 */
	if ((error = suser_cred(p->p_ucred, PRISON_ROOT)) != 0)
		return (error);

	if ((error = nlookup(nd)) != 0)
		return (error);
	ncp = nd->nl_ncp;

	/*
	 * Disallow open directory descriptors (fchdir() breakouts).
	 */
	if (chroot_allow_open_directories == 0 ||
	   (chroot_allow_open_directories == 1 && fdp->fd_rdir != rootvnode)) {
		if ((error = chroot_refuse_vdir_fds(fdp)) != 0)
			return (error);
	}
	if ((vp = ncp->nc_vp) == NULL)
		return (ENOENT);

	if ((error = vget(vp, LK_SHARED, td)) != 0)
		return (error);

	/*
	 * Check the validity of vp as a directory to change to and 
	 * associate it with rdir/jdir.
	 */
	error = checkvp_chdir(vp, td);
	VOP_UNLOCK(vp, 0, td);	/* leave reference intact */
	if (error == 0) {
		vrele(fdp->fd_rdir);
		fdp->fd_rdir = vp;	/* reference inherited by fd_rdir */
		cache_drop(fdp->fd_nrdir);
		fdp->fd_nrdir = cache_hold(ncp);
		if (fdp->fd_jdir == NULL) {
			fdp->fd_jdir = vp;
			vref(fdp->fd_jdir);
			fdp->fd_njdir = cache_hold(ncp);
		}
	} else {
		vrele(vp);
	}
	return (error);
}

/*
 * chroot_args(char *path)
 *
 * Change notion of root (``/'') directory.
 */
/* ARGSUSED */
int
chroot(struct chroot_args *uap)
{
	struct thread *td = curthread;
	struct nlookupdata nd;
	int error;

	KKASSERT(td->td_proc);
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0) 
		error = kern_chroot(&nd);
	nlookup_done(&nd);
	return (error);
}

/*
 * Common routine for chroot and chdir.  Given a locked, referenced vnode,
 * determine whether it is legal to chdir to the vnode.  The vnode's state
 * is not changed by this call.
 */
int
checkvp_chdir(struct vnode *vp, struct thread *td)
{
	int error;

	if (vp->v_type != VDIR)
		error = ENOTDIR;
	else
		error = VOP_ACCESS(vp, VEXEC, td->td_proc->p_ucred, td);
	return (error);
}

int
kern_open(struct nlookupdata *nd, int oflags, int mode, int *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	int cmode, flags;
	struct file *nfp;
	struct file *fp;
	struct vnode *vp;
	int type, indx, error;
	struct flock lf;

	if ((oflags & O_ACCMODE) == O_ACCMODE)
		return (EINVAL);
	flags = FFLAGS(oflags);
	error = falloc(p, &nfp, NULL);
	if (error)
		return (error);
	fp = nfp;
	cmode = ((mode &~ fdp->fd_cmask) & ALLPERMS) &~ S_ISTXT;

	/*
	 * XXX p_dupfd is a real mess.  It allows a device to return a
	 * file descriptor to be duplicated rather then doing the open
	 * itself.
	 */
	p->p_dupfd = -1;

	/*
	 * Call vn_open() to do the lookup and assign the vnode to the 
	 * file pointer.  vn_open() does not change the ref count on fp
	 * and the vnode, on success, will be inherited by the file pointer
	 * and unlocked.
	 */
	nd->nl_flags |= NLC_LOCKVP;
	error = vn_open(nd, fp, flags, cmode);
	nlookup_done(nd);
	if (error) {
		/*
		 * handle special fdopen() case.  bleh.  dupfdopen() is
		 * responsible for dropping the old contents of ofiles[indx]
		 * if it succeeds.
		 *
		 * Note that if fsetfd() succeeds it will add a ref to fp
		 * which represents the fd_ofiles[] assignment.  We must still
		 * drop our reference.
		 */
		if ((error == ENODEV || error == ENXIO) && p->p_dupfd >= 0) {
			if (fsetfd(p, fp, &indx) == 0) {
				error = dupfdopen(fdp, indx, p->p_dupfd, flags, error);
				if (error == 0) {
					*res = indx;
					fdrop(fp, td);	/* our ref */
					return (0);
				}
				if (fdp->fd_ofiles[indx] == fp) {
					fdp->fd_ofiles[indx] = NULL;
					fdrop(fp, td);	/* fd_ofiles[] ref */
				}
			}
		}
		fdrop(fp, td);	/* our ref */
		if (error == ERESTART)
			error = EINTR;
		return (error);
	}

	/*
	 * ref the vnode for ourselves so it can't be ripped out from under
	 * is.  XXX need an ND flag to request that the vnode be returned
	 * anyway.
	 */
	vp = (struct vnode *)fp->f_data;
	vref(vp);
	if ((error = fsetfd(p, fp, &indx)) != 0) {
		fdrop(fp, td);
		vrele(vp);
		return (error);
	}

	/*
	 * If no error occurs the vp will have been assigned to the file
	 * pointer.
	 */
	p->p_dupfd = 0;

	/*
	 * There should be 2 references on the file, one from the descriptor
	 * table, and one for us.
	 *
	 * Handle the case where someone closed the file (via its file
	 * descriptor) while we were blocked.  The end result should look
	 * like opening the file succeeded but it was immediately closed.
	 */
	if (fp->f_count == 1) {
		KASSERT(fdp->fd_ofiles[indx] != fp,
		    ("Open file descriptor lost all refs"));
		vrele(vp);
		fo_close(fp, td);
		fdrop(fp, td);
		*res = indx;
		return 0;
	}

	if (flags & (O_EXLOCK | O_SHLOCK)) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		if (flags & O_EXLOCK)
			lf.l_type = F_WRLCK;
		else
			lf.l_type = F_RDLCK;
		type = F_FLOCK;
		if ((flags & FNONBLOCK) == 0)
			type |= F_WAIT;

		if ((error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, type)) != 0) {
			/*
			 * lock request failed.  Normally close the descriptor
			 * but handle the case where someone might have dup()d
			 * it when we weren't looking.  One reference is
			 * owned by the descriptor array, the other by us.
			 */
			vrele(vp);
			if (fdp->fd_ofiles[indx] == fp) {
				fdp->fd_ofiles[indx] = NULL;
				fdrop(fp, td);
			}
			fdrop(fp, td);
			return (error);
		}
		fp->f_flag |= FHASLOCK;
	}
	/* assert that vn_open created a backing object if one is needed */
	KASSERT(!vn_canvmio(vp) || VOP_GETVOBJECT(vp, NULL) == 0,
		("open: vmio vnode has no backing object after vn_open"));

	vrele(vp);

	/*
	 * release our private reference, leaving the one associated with the
	 * descriptor table intact.
	 */
	fdrop(fp, td);
	*res = indx;
	return (0);
}

/*
 * open_args(char *path, int flags, int mode)
 *
 * Check permissions, allocate an open file structure,
 * and call the device open routine if any.
 */
int
open(struct open_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_open(&nd, uap->flags,
				    uap->mode, &uap->sysmsg_result);
	}
	nlookup_done(&nd);
	return (error);
}

int
kern_mknod(struct nlookupdata *nd, int mode, int dev)
{
	struct namecache *ncp;
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	int whiteout = 0;

	KKASSERT(p);

	switch (mode & S_IFMT) {
	case S_IFCHR:
	case S_IFBLK:
		error = suser(td);
		break;
	default:
		error = suser_cred(p->p_ucred, PRISON_ROOT);
		break;
	}
	if (error)
		return (error);

	bwillwrite();
	nd->nl_flags |= NLC_CREATE;
	if ((error = nlookup(nd)) != 0)
		return (error);
	ncp = nd->nl_ncp;
	if (ncp->nc_vp)
		return (EEXIST);

	VATTR_NULL(&vattr);
	vattr.va_mode = (mode & ALLPERMS) &~ p->p_fd->fd_cmask;
	vattr.va_rdev = dev;
	whiteout = 0;

	switch (mode & S_IFMT) {
	case S_IFMT:	/* used by badsect to flag bad sectors */
		vattr.va_type = VBAD;
		break;
	case S_IFCHR:
		vattr.va_type = VCHR;
		break;
	case S_IFBLK:
		vattr.va_type = VBLK;
		break;
	case S_IFWHT:
		whiteout = 1;
		break;
	default:
		error = EINVAL;
		break;
	}
	if (error == 0) {
		if (whiteout) {
			error = VOP_NWHITEOUT(ncp, nd->nl_cred, NAMEI_CREATE);
		} else {
			vp = NULL;
			error = VOP_NMKNOD(ncp, &vp, nd->nl_cred, &vattr);
			if (error == 0)
				vput(vp);
		}
	}
	return (error);
}

/*
 * mknod_args(char *path, int mode, int dev)
 *
 * Create a special file.
 */
int
mknod(struct mknod_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_mknod(&nd, uap->mode, uap->dev);
	nlookup_done(&nd);
	return (error);
}

int
kern_mkfifo(struct nlookupdata *nd, int mode)
{
	struct namecache *ncp;
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vattr vattr;
	struct vnode *vp;
	int error;

	bwillwrite();

	nd->nl_flags |= NLC_CREATE;
	if ((error = nlookup(nd)) != 0)
		return (error);
	ncp = nd->nl_ncp;
	if (ncp->nc_vp)
		return (EEXIST);

	VATTR_NULL(&vattr);
	vattr.va_type = VFIFO;
	vattr.va_mode = (mode & ALLPERMS) &~ p->p_fd->fd_cmask;
	vp = NULL;
	error = VOP_NMKNOD(ncp, &vp, nd->nl_cred, &vattr);
	if (error == 0)
		vput(vp);
	return (error);
}

/*
 * mkfifo_args(char *path, int mode)
 *
 * Create a named pipe.
 */
int
mkfifo(struct mkfifo_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_mkfifo(&nd, uap->mode);
	nlookup_done(&nd);
	return (error);
}

int
kern_link(struct nlookupdata *nd, struct nlookupdata *linknd)
{
	struct thread *td = curthread;
	struct vnode *vp;
	int error;

	/*
	 * Lookup the source and obtained a locked vnode.
	 *
	 * XXX relookup on vget failure / race ?
	 */
	bwillwrite();
	if ((error = nlookup(nd)) != 0)
		return (error);
	vp = nd->nl_ncp->nc_vp;
	KKASSERT(vp != NULL);
	if (vp->v_type == VDIR)
		return (EPERM);		/* POSIX */
	if ((error = vget(vp, LK_EXCLUSIVE, td)) != 0)
		return (error);

	/*
	 * Unlock the source so we can lookup the target without deadlocking
	 * (XXX vp is locked already, possible other deadlock?).  The target
	 * must not exist.
	 */
	KKASSERT(nd->nl_flags & NLC_NCPISLOCKED);
	nd->nl_flags &= ~NLC_NCPISLOCKED;
	cache_unlock(nd->nl_ncp);

	linknd->nl_flags |= NLC_CREATE;
	if ((error = nlookup(linknd)) != 0) {
		vput(vp);
		return (error);
	}
	if (linknd->nl_ncp->nc_vp) {
		vput(vp);
		return (EEXIST);
	}

	/*
	 * Finally run the new API VOP.
	 */
	error = VOP_NLINK(linknd->nl_ncp, vp, linknd->nl_cred);
	vput(vp);
	return (error);
}

/*
 * link_args(char *path, char *link)
 *
 * Make a hard file link.
 */
int
link(struct link_args *uap)
{
	struct nlookupdata nd, linknd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = nlookup_init(&linknd, uap->link, UIO_USERSPACE, 0);
		if (error == 0)
			error = kern_link(&nd, &linknd);
		nlookup_done(&linknd);
	}
	nlookup_done(&nd);
	return (error);
}

int
kern_symlink(struct nlookupdata *nd, char *path, int mode)
{
	struct namecache *ncp;
	struct vattr vattr;
	struct vnode *vp;
	int error;

	bwillwrite();
	nd->nl_flags |= NLC_CREATE;
	if ((error = nlookup(nd)) != 0)
		return (error);
	ncp = nd->nl_ncp;
	if (ncp->nc_vp)
		return (EEXIST);

	VATTR_NULL(&vattr);
	vattr.va_mode = mode;
	error = VOP_NSYMLINK(ncp, &vp, nd->nl_cred, &vattr, path);
	if (error == 0)
		vput(vp);
	return (error);
}

/*
 * symlink(char *path, char *link)
 *
 * Make a symbolic link.
 */
int
symlink(struct symlink_args *uap)
{
	struct thread *td = curthread;
	struct nlookupdata nd;
	char *path;
	int error;
	int mode;

	path = zalloc(namei_zone);
	error = copyinstr(uap->path, path, MAXPATHLEN, NULL);
	if (error == 0) {
		error = nlookup_init(&nd, uap->link, UIO_USERSPACE, 0);
		if (error == 0) {
			mode = ACCESSPERMS & ~td->td_proc->p_fd->fd_cmask;
			error = kern_symlink(&nd, path, mode);
		}
		nlookup_done(&nd);
	}
	zfree(namei_zone, path);
	return (error);
}

/*
 * undelete_args(char *path)
 *
 * Delete a whiteout from the filesystem.
 */
/* ARGSUSED */
int
undelete(struct undelete_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, SCARG(uap, path), UIO_USERSPACE, 0);
	bwillwrite();
	nd.nl_flags |= NLC_DELETE;
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = VOP_NWHITEOUT(nd.nl_ncp, nd.nl_cred, NAMEI_DELETE);
	nlookup_done(&nd);
	return (error);
}

int
kern_unlink(struct nlookupdata *nd)
{
	struct namecache *ncp;
	int error;

	bwillwrite();
	nd->nl_flags |= NLC_DELETE;
	if ((error = nlookup(nd)) != 0)
		return (error);
	ncp = nd->nl_ncp;
	error = VOP_NREMOVE(ncp, nd->nl_cred);
	return (error);
}

/*
 * unlink_args(char *path)
 *
 * Delete a name from the filesystem.
 */
int
unlink(struct unlink_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_unlink(&nd);
	nlookup_done(&nd);
	return (error);
}

int
kern_lseek(int fd, off_t offset, int whence, off_t *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct vattr vattr;
	int error;

	if (fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_ofiles[fd]) == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_VNODE)
		return (ESPIPE);
	switch (whence) {
	case L_INCR:
		fp->f_offset += offset;
		break;
	case L_XTND:
		error=VOP_GETATTR((struct vnode *)fp->f_data, &vattr, td);
		if (error)
			return (error);
		fp->f_offset = offset + vattr.va_size;
		break;
	case L_SET:
		fp->f_offset = offset;
		break;
	default:
		return (EINVAL);
	}
	*res = fp->f_offset;
	return (0);
}

/*
 * lseek_args(int fd, int pad, off_t offset, int whence)
 *
 * Reposition read/write file offset.
 */
int
lseek(struct lseek_args *uap)
{
	int error;

	error = kern_lseek(uap->fd, uap->offset, uap->whence,
	    &uap->sysmsg_offset);

	return (error);
}

int
kern_access(struct nlookupdata *nd, int aflags)
{
	struct thread *td = curthread;
	struct vnode *vp;
	int error, flags;

	if ((error = nlookup(nd)) != 0)
		return (error);
retry:
	error = cache_vget(nd->nl_ncp, nd->nl_cred, LK_EXCLUSIVE, &vp);
	if (error)
		return (error);

	/* Flags == 0 means only check for existence. */
	if (aflags) {
		flags = 0;
		if (aflags & R_OK)
			flags |= VREAD;
		if (aflags & W_OK)
			flags |= VWRITE;
		if (aflags & X_OK)
			flags |= VEXEC;
		if ((flags & VWRITE) == 0 || (error = vn_writechk(vp)) == 0)
			error = VOP_ACCESS(vp, flags, nd->nl_cred, td);

		/*
		 * If the file handle is stale we have to re-resolve the
		 * entry.  This is a hack at the moment.
		 */
		if (error == ESTALE) {
			cache_setunresolved(nd->nl_ncp);
			error = cache_resolve(nd->nl_ncp, nd->nl_cred);
			if (error == 0) {
				vput(vp);
				vp = NULL;
				goto retry;
			}
		}
	}
	vput(vp);
	return (error);
}

/*
 * access_args(char *path, int flags)
 *
 * Check access permissions.
 */
int
access(struct access_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_access(&nd, uap->flags);
	nlookup_done(&nd);
	return (error);
}

int
kern_stat(struct nlookupdata *nd, struct stat *st)
{
	int error;
	struct vnode *vp;
	thread_t td;

	if ((error = nlookup(nd)) != 0)
		return (error);
again:
	if ((vp = nd->nl_ncp->nc_vp) == NULL)
		return (ENOENT);

	td = curthread;
	if ((error = vget(vp, LK_SHARED, td)) != 0)
		return (error);
	error = vn_stat(vp, st, td);

	/*
	 * If the file handle is stale we have to re-resolve the entry.  This
	 * is a hack at the moment.
	 */
	if (error == ESTALE) {
		cache_setunresolved(nd->nl_ncp);
		error = cache_resolve(nd->nl_ncp, nd->nl_cred);
		if (error == 0) {
			vput(vp);
			goto again;
		}
	}
	vput(vp);
	return (error);
}

/*
 * stat_args(char *path, struct stat *ub)
 *
 * Get file status; this version follows links.
 */
int
stat(struct stat_args *uap)
{
	struct nlookupdata nd;
	struct stat st;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_stat(&nd, &st);
		if (error == 0)
			error = copyout(&st, uap->ub, sizeof(*uap->ub));
	}
	nlookup_done(&nd);
	return (error);
}

/*
 * lstat_args(char *path, struct stat *ub)
 *
 * Get file status; this version does not follow links.
 */
int
lstat(struct lstat_args *uap)
{
	struct nlookupdata nd;
	struct stat st;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		error = kern_stat(&nd, &st);
		if (error == 0)
			error = copyout(&st, uap->ub, sizeof(*uap->ub));
	}
	nlookup_done(&nd);
	return (error);
}

void
cvtnstat(sb, nsb)
	struct stat *sb;
	struct nstat *nsb;
{
	nsb->st_dev = sb->st_dev;
	nsb->st_ino = sb->st_ino;
	nsb->st_mode = sb->st_mode;
	nsb->st_nlink = sb->st_nlink;
	nsb->st_uid = sb->st_uid;
	nsb->st_gid = sb->st_gid;
	nsb->st_rdev = sb->st_rdev;
	nsb->st_atimespec = sb->st_atimespec;
	nsb->st_mtimespec = sb->st_mtimespec;
	nsb->st_ctimespec = sb->st_ctimespec;
	nsb->st_size = sb->st_size;
	nsb->st_blocks = sb->st_blocks;
	nsb->st_blksize = sb->st_blksize;
	nsb->st_flags = sb->st_flags;
	nsb->st_gen = sb->st_gen;
	nsb->st_qspare[0] = sb->st_qspare[0];
	nsb->st_qspare[1] = sb->st_qspare[1];
}

/*
 * nstat_args(char *path, struct nstat *ub)
 */
/* ARGSUSED */
int
nstat(struct nstat_args *uap)
{
	struct thread *td = curthread;
	struct vnode *vp;
	struct stat sb;
	struct nstat nsb;
	struct nlookupdata nd;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, SCARG(uap, path), UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(nd.nl_ncp, nd.nl_cred, LK_EXCLUSIVE, &vp);
	nlookup_done(&nd);
	if (error == 0) {
		error = vn_stat(vp, &sb, td);
		vput(vp);
		if (error == 0) {
			cvtnstat(&sb, &nsb);
			error = copyout(&nsb, SCARG(uap, ub), sizeof(nsb));
		}
	}
	return (error);
}

/*
 * lstat_args(char *path, struct stat *ub)
 *
 * Get file status; this version does not follow links.
 */
/* ARGSUSED */
int
nlstat(struct nlstat_args *uap)
{
	struct thread *td = curthread;
	struct vnode *vp;
	struct stat sb;
	struct nstat nsb;
	struct nlookupdata nd;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, SCARG(uap, path), UIO_USERSPACE, 0);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(nd.nl_ncp, nd.nl_cred, LK_EXCLUSIVE, &vp);
	nlookup_done(&nd);
	if (error == 0) {
		error = vn_stat(vp, &sb, td);
		vput(vp);
		if (error == 0) {
			cvtnstat(&sb, &nsb);
			error = copyout(&nsb, SCARG(uap, ub), sizeof(nsb));
		}
	}
	return (error);
}

/*
 * pathconf_Args(char *path, int name)
 *
 * Get configurable pathname variables.
 */
/* ARGSUSED */
int
pathconf(struct pathconf_args *uap)
{
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, SCARG(uap, path), UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(nd.nl_ncp, nd.nl_cred, LK_EXCLUSIVE, &vp);
	nlookup_done(&nd);
	if (error == 0) {
		error = VOP_PATHCONF(vp, SCARG(uap, name), uap->sysmsg_fds);
		vput(vp);
	}
	return (error);
}

/*
 * XXX: daver
 * kern_readlink isn't properly split yet.  There is a copyin burried
 * in VOP_READLINK().
 */
int
kern_readlink(struct nlookupdata *nd, char *buf, int count, int *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct iovec aiov;
	struct uio auio;
	int error;

	if ((error = nlookup(nd)) != 0)
		return (error);
	error = cache_vget(nd->nl_ncp, nd->nl_cred, LK_EXCLUSIVE, &vp);
	if (error)
		return (error);
	if (vp->v_type != VLNK) {
		error = EINVAL;
	} else {
		aiov.iov_base = buf;
		aiov.iov_len = count;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_td = td;
		auio.uio_resid = count;
		error = VOP_READLINK(vp, &auio, p->p_ucred);
	}
	vput(vp);
	*res = count - auio.uio_resid;
	return (error);
}

/*
 * readlink_args(char *path, char *buf, int count)
 *
 * Return target name of a symbolic link.
 */
int
readlink(struct readlink_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		error = kern_readlink(&nd, uap->buf, uap->count,
					&uap->sysmsg_result);
	}
	nlookup_done(&nd);
	return (error);
}

static int
setfflags(struct vnode *vp, int flags)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;
	struct vattr vattr;

	/*
	 * Prevent non-root users from setting flags on devices.  When
	 * a device is reused, users can retain ownership of the device
	 * if they are allowed to set flags and programs assume that
	 * chown can't fail when done as root.
	 */
	if ((vp->v_type == VCHR || vp->v_type == VBLK) && 
	    ((error = suser_cred(p->p_ucred, PRISON_ROOT)) != 0))
		return (error);

	/*
	 * note: vget is required for any operation that might mod the vnode
	 * so VINACTIVE is properly cleared.
	 */
	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	if ((error = vget(vp, LK_EXCLUSIVE, td)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_flags = flags;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, td);
		vput(vp);
	}
	return (error);
}

/*
 * chflags(char *path, int flags)
 *
 * Change flags of a file given a path name.
 */
/* ARGSUSED */
int
chflags(struct chflags_args *uap)
{
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, SCARG(uap, path), UIO_USERSPACE, NLC_FOLLOW);
	/* XXX Add NLC flag indicating modifying operation? */
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(nd.nl_ncp, nd.nl_cred, &vp);
	nlookup_done(&nd);
	if (error == 0) {
		error = setfflags(vp, SCARG(uap, flags));
		vrele(vp);
	}
	return (error);
}

/*
 * fchflags_args(int fd, int flags)
 *
 * Change flags of a file given a file descriptor.
 */
/* ARGSUSED */
int
fchflags(struct fchflags_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	return setfflags((struct vnode *) fp->f_data, SCARG(uap, flags));
}

static int
setfmode(struct vnode *vp, int mode)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;
	struct vattr vattr;

	/*
	 * note: vget is required for any operation that might mod the vnode
	 * so VINACTIVE is properly cleared.
	 */
	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	if ((error = vget(vp, LK_EXCLUSIVE, td)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_mode = mode & ALLPERMS;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, td);
		vput(vp);
	}
	return error;
}

int
kern_chmod(struct nlookupdata *nd, int mode)
{
	struct vnode *vp;
	int error;

	/* XXX Add NLC flag indicating modifying operation? */
	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((error = cache_vref(nd->nl_ncp, nd->nl_cred, &vp)) != 0)
		return (error);
	error = setfmode(vp, mode);
	vrele(vp);
	return (error);
}

/*
 * chmod_args(char *path, int mode)
 *
 * Change mode of a file given path name.
 */
/* ARGSUSED */
int
chmod(struct chmod_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_chmod(&nd, uap->mode);
	nlookup_done(&nd);
	return (error);
}

/*
 * lchmod_args(char *path, int mode)
 *
 * Change mode of a file given path name (don't follow links.)
 */
/* ARGSUSED */
int
lchmod(struct lchmod_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_chmod(&nd, uap->mode);
	nlookup_done(&nd);
	return (error);
}

/*
 * fchmod_args(int fd, int mode)
 *
 * Change mode of a file given a file descriptor.
 */
/* ARGSUSED */
int
fchmod(struct fchmod_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	return setfmode((struct vnode *)fp->f_data, SCARG(uap, mode));
}

static int
setfown(struct vnode *vp, uid_t uid, gid_t gid)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;
	struct vattr vattr;

	/*
	 * note: vget is required for any operation that might mod the vnode
	 * so VINACTIVE is properly cleared.
	 */
	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	if ((error = vget(vp, LK_EXCLUSIVE, td)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_uid = uid;
		vattr.va_gid = gid;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, td);
		vput(vp);
	}
	return error;
}

int
kern_chown(struct nlookupdata *nd, int uid, int gid)
{
	struct vnode *vp;
	int error;

	/* XXX Add NLC flag indicating modifying operation? */
	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((error = cache_vref(nd->nl_ncp, nd->nl_cred, &vp)) != 0)
		return (error);
	error = setfown(vp, uid, gid);
	vrele(vp);
	return (error);
}

/*
 * chown(char *path, int uid, int gid)
 *
 * Set ownership given a path name.
 */
int
chown(struct chown_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_chown(&nd, uap->uid, uap->gid);
	nlookup_done(&nd);
	return (error);
}

/*
 * lchown_args(char *path, int uid, int gid)
 *
 * Set ownership given a path name, do not cross symlinks.
 */
int
lchown(struct lchown_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_chown(&nd, uap->uid, uap->gid);
	nlookup_done(&nd);
	return (error);
}

/*
 * fchown_args(int fd, int uid, int gid)
 *
 * Set ownership given a file descriptor.
 */
/* ARGSUSED */
int
fchown(struct fchown_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	return setfown((struct vnode *)fp->f_data,
		SCARG(uap, uid), SCARG(uap, gid));
}

static int
getutimes(const struct timeval *tvp, struct timespec *tsp)
{
	struct timeval tv[2];

	if (tvp == NULL) {
		microtime(&tv[0]);
		TIMEVAL_TO_TIMESPEC(&tv[0], &tsp[0]);
		tsp[1] = tsp[0];
	} else {
		TIMEVAL_TO_TIMESPEC(&tvp[0], &tsp[0]);
		TIMEVAL_TO_TIMESPEC(&tvp[1], &tsp[1]);
	}
	return 0;
}

static int
setutimes(struct vnode *vp, const struct timespec *ts, int nullflag)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;
	struct vattr vattr;

	/*
	 * note: vget is required for any operation that might mod the vnode
	 * so VINACTIVE is properly cleared.
	 */
	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	if ((error = vget(vp, LK_EXCLUSIVE, td)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_atime = ts[0];
		vattr.va_mtime = ts[1];
		if (nullflag)
			vattr.va_vaflags |= VA_UTIMES_NULL;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, td);
		vput(vp);
	}
	return error;
}

int
kern_utimes(struct nlookupdata *nd, struct timeval *tptr)
{
	struct timespec ts[2];
	struct vnode *vp;
	int error;

	if ((error = getutimes(tptr, ts)) != 0)
		return (error);
	/* XXX Add NLC flag indicating modifying operation? */
	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((error = cache_vref(nd->nl_ncp, nd->nl_cred, &vp)) != 0)
		return (error);
	error = setutimes(vp, ts, tptr == NULL);
	vrele(vp);
	return (error);
}

/*
 * utimes_args(char *path, struct timeval *tptr)
 *
 * Set the access and modification times of a file.
 */
int
utimes(struct utimes_args *uap)
{
	struct timeval tv[2];
	struct nlookupdata nd;
	int error;

	if (uap->tptr) {
 		error = copyin(uap->tptr, tv, sizeof(tv));
		if (error)
			return (error);
	}
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_utimes(&nd, uap->tptr ? tv : NULL);
	nlookup_done(&nd);
	return (error);
}

/*
 * lutimes_args(char *path, struct timeval *tptr)
 *
 * Set the access and modification times of a file.
 */
int
lutimes(struct lutimes_args *uap)
{
	struct timeval tv[2];
	struct nlookupdata nd;
	int error;

	if (uap->tptr) {
		error = copyin(uap->tptr, tv, sizeof(tv));
		if (error)
			return (error);
	}
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_utimes(&nd, uap->tptr ? tv : NULL);
	nlookup_done(&nd);
	return (error);
}

int
kern_futimes(int fd, struct timeval *tptr)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct timespec ts[2];
	struct file *fp;
	int error;

	error = getutimes(tptr, ts);
	if (error)
		return (error);
	error = getvnode(p->p_fd, fd, &fp);
	if (error)
		return (error);
	error =  setutimes((struct vnode *)fp->f_data, ts, tptr == NULL);
	return (error);
}

/*
 * futimes_args(int fd, struct timeval *tptr)
 *
 * Set the access and modification times of a file.
 */
int
futimes(struct futimes_args *uap)
{
	struct timeval tv[2];
	int error;

	if (uap->tptr) {
		error = copyin(uap->tptr, tv, sizeof(tv));
		if (error)
			return (error);
	}

	error = kern_futimes(uap->fd, uap->tptr ? tv : NULL);

	return (error);
}

int
kern_truncate(struct nlookupdata *nd, off_t length)
{
	struct vnode *vp;
	struct vattr vattr;
	int error;

	if (length < 0)
		return(EINVAL);
	/* XXX Add NLC flag indicating modifying operation? */
	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((error = cache_vref(nd->nl_ncp, nd->nl_cred, &vp)) != 0)
		return (error);
	VOP_LEASE(vp, nd->nl_td, nd->nl_cred, LEASE_WRITE);
	if ((error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, nd->nl_td)) != 0) {
		vrele(vp);
		return (error);
	}
	if (vp->v_type == VDIR) {
		error = EISDIR;
	} else if ((error = vn_writechk(vp)) == 0 &&
	    (error = VOP_ACCESS(vp, VWRITE, nd->nl_cred, nd->nl_td)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_size = length;
		error = VOP_SETATTR(vp, &vattr, nd->nl_cred, nd->nl_td);
	}
	vput(vp);
	return (error);
}

/*
 * truncate(char *path, int pad, off_t length)
 *
 * Truncate a file given its path name.
 */
int
truncate(struct truncate_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_truncate(&nd, uap->length);
	nlookup_done(&nd);
	return error;
}

int
kern_ftruncate(int fd, off_t length)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vattr vattr;
	struct vnode *vp;
	struct file *fp;
	int error;

	if (length < 0)
		return(EINVAL);
	if ((error = getvnode(p->p_fd, fd, &fp)) != 0)
		return (error);
	if ((fp->f_flag & FWRITE) == 0)
		return (EINVAL);
	vp = (struct vnode *)fp->f_data;
	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	if (vp->v_type == VDIR)
		error = EISDIR;
	else if ((error = vn_writechk(vp)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_size = length;
		error = VOP_SETATTR(vp, &vattr, fp->f_cred, td);
	}
	VOP_UNLOCK(vp, 0, td);
	return (error);
}

/*
 * ftruncate_args(int fd, int pad, off_t length)
 *
 * Truncate a file given a file descriptor.
 */
int
ftruncate(struct ftruncate_args *uap)
{
	int error;

	error = kern_ftruncate(uap->fd, uap->length);

	return (error);
}

/*
 * fsync(int fd)
 *
 * Sync an open file.
 */
/* ARGSUSED */
int
fsync(struct fsync_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct file *fp;
	vm_object_t obj;
	int error;

	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	vp = (struct vnode *)fp->f_data;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	if (VOP_GETVOBJECT(vp, &obj) == 0)
		vm_object_page_clean(obj, 0, 0, 0);
	if ((error = VOP_FSYNC(vp, MNT_WAIT, td)) == 0 &&
	    vp->v_mount && (vp->v_mount->mnt_flag & MNT_SOFTDEP) &&
	    bioops.io_fsync)
		error = (*bioops.io_fsync)(vp);
	VOP_UNLOCK(vp, 0, td);
	return (error);
}

int
kern_rename(struct nlookupdata *fromnd, struct nlookupdata *tond)
{
	struct namecache *fncpd;
	struct namecache *tncpd;
	struct namecache *ncp;
	struct mount *mp;
	int error;

	bwillwrite();
	if ((error = nlookup(fromnd)) != 0)
		return (error);
	if ((fncpd = fromnd->nl_ncp->nc_parent) == NULL)
		return (ENOENT);
	cache_hold(fncpd);

	/*
	 * unlock the source ncp so we can lookup the target ncp without
	 * deadlocking.  The target may or may not exist so we do not check
	 * for a target vp like kern_mkdir() and other creation functions do.
	 *
	 * The source and target directories are ref'd and rechecked after
	 * everything is relocked to determine if the source or target file
	 * has been renamed.
	 */
	KKASSERT(fromnd->nl_flags & NLC_NCPISLOCKED);
	fromnd->nl_flags &= ~NLC_NCPISLOCKED;
	cache_unlock(fromnd->nl_ncp);

	tond->nl_flags |= NLC_CREATE;
	if ((error = nlookup(tond)) != 0) {
		cache_drop(fncpd);
		return (error);
	}
	if ((tncpd = tond->nl_ncp->nc_parent) == NULL) {
		cache_drop(fncpd);
		return (ENOENT);
	}
	cache_hold(tncpd);

	/*
	 * If the source and target are the same there is nothing to do
	 */
	if (fromnd->nl_ncp == tond->nl_ncp) {
		cache_drop(fncpd);
		cache_drop(tncpd);
		return (0);
	}

	/*
	 * relock the source ncp
	 */
	if (cache_lock_nonblock(fromnd->nl_ncp) == 0) {
		cache_resolve(fromnd->nl_ncp, fromnd->nl_cred);
	} else if (fromnd->nl_ncp > tond->nl_ncp) {
		cache_lock(fromnd->nl_ncp);
		cache_resolve(fromnd->nl_ncp, fromnd->nl_cred);
	} else {
		cache_unlock(tond->nl_ncp);
		cache_lock(fromnd->nl_ncp);
		cache_resolve(fromnd->nl_ncp, fromnd->nl_cred);
		cache_lock(tond->nl_ncp);
		cache_resolve(tond->nl_ncp, tond->nl_cred);
	}
	fromnd->nl_flags |= NLC_NCPISLOCKED;

	/*
	 * make sure the parent directories linkages are the same
	 */
	if (fncpd != fromnd->nl_ncp->nc_parent ||
	    tncpd != tond->nl_ncp->nc_parent) {
		cache_drop(fncpd);
		cache_drop(tncpd);
		return (ENOENT);
	}

	/*
	 * Both the source and target must be within the same filesystem and
	 * in the same filesystem as their parent directories within the
	 * namecache topology.
	 */
	mp = fncpd->nc_mount;
	if (mp != tncpd->nc_mount || mp != fromnd->nl_ncp->nc_mount ||
	    mp != tond->nl_ncp->nc_mount) {
		cache_drop(fncpd);
		cache_drop(tncpd);
		return (EXDEV);
	}

	/*
	 * If the target exists and either the source or target is a directory,
	 * then both must be directories.
	 */
	if (tond->nl_ncp->nc_vp) {
		if (fromnd->nl_ncp->nc_vp->v_type == VDIR) {
			if (tond->nl_ncp->nc_vp->v_type != VDIR)
				error = ENOTDIR;
		} else if (tond->nl_ncp->nc_vp->v_type == VDIR) {
			error = EISDIR;
		}
	}

	/*
	 * You cannot rename a source into itself or a subdirectory of itself.
	 * We check this by travsersing the target directory upwards looking
	 * for a match against the source.
	 */
	if (error == 0) {
		for (ncp = tncpd; ncp; ncp = ncp->nc_parent) {
			if (fromnd->nl_ncp == ncp) {
				error = EINVAL;
				break;
			}
		}
	}

	cache_drop(fncpd);
	cache_drop(tncpd);
	if (error)
		return (error);
	error = VOP_NRENAME(fromnd->nl_ncp, tond->nl_ncp, tond->nl_cred);
	return (error);
}

/*
 * rename_args(char *from, char *to)
 *
 * Rename files.  Source and destination must either both be directories,
 * or both not be directories.  If target is a directory, it must be empty.
 */
int
rename(struct rename_args *uap)
{
	struct nlookupdata fromnd, tond;
	int error;

	error = nlookup_init(&fromnd, uap->from, UIO_USERSPACE, 0);
	if (error == 0) {
		error = nlookup_init(&tond, uap->to, UIO_USERSPACE, 0);
		if (error == 0)
			error = kern_rename(&fromnd, &tond);
		nlookup_done(&tond);
	}
	nlookup_done(&fromnd);
	return (error);
}

int
kern_mkdir(struct nlookupdata *nd, int mode)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct namecache *ncp;
	struct vnode *vp;
	struct vattr vattr;
	int error;

	bwillwrite();
	nd->nl_flags |= NLC_WILLBEDIR | NLC_CREATE;
	if ((error = nlookup(nd)) != 0)
		return (error);

	ncp = nd->nl_ncp;
	if (ncp->nc_vp)
		return (EEXIST);

	VATTR_NULL(&vattr);
	vattr.va_type = VDIR;
	vattr.va_mode = (mode & ACCESSPERMS) &~ p->p_fd->fd_cmask;

	vp = NULL;
	error = VOP_NMKDIR(ncp, &vp, p->p_ucred, &vattr);
	if (error == 0)
		vput(vp);
	return (error);
}

/*
 * mkdir_args(char *path, int mode)
 *
 * Make a directory file.
 */
/* ARGSUSED */
int
mkdir(struct mkdir_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_mkdir(&nd, uap->mode);
	nlookup_done(&nd);
	return (error);
}

int
kern_rmdir(struct nlookupdata *nd)
{
	struct namecache *ncp;
	int error;

	bwillwrite();
	nd->nl_flags |= NLC_DELETE;
	if ((error = nlookup(nd)) != 0)
		return (error);

	ncp = nd->nl_ncp;
	error = VOP_NRMDIR(ncp, nd->nl_cred);
	return (error);
}

/*
 * rmdir_args(char *path)
 *
 * Remove a directory file.
 */
/* ARGSUSED */
int
rmdir(struct rmdir_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_rmdir(&nd);
	nlookup_done(&nd);
	return (error);
}

int
kern_getdirentries(int fd, char *buf, u_int count, long *basep, int *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct file *fp;
	struct uio auio;
	struct iovec aiov;
	long loff;
	int error, eofflag;

	if ((error = getvnode(p->p_fd, fd, &fp)) != 0)
		return (error);
	if ((fp->f_flag & FREAD) == 0)
		return (EBADF);
	vp = (struct vnode *)fp->f_data;
unionread:
	if (vp->v_type != VDIR)
		return (EINVAL);
	aiov.iov_base = buf;
	aiov.iov_len = count;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;
	auio.uio_resid = count;
	/* vn_lock(vp, LK_SHARED | LK_RETRY, td); */
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	loff = auio.uio_offset = fp->f_offset;
	error = VOP_READDIR(vp, &auio, fp->f_cred, &eofflag, NULL, NULL);
	fp->f_offset = auio.uio_offset;
	VOP_UNLOCK(vp, 0, td);
	if (error)
		return (error);
	if (count == auio.uio_resid) {
		if (union_dircheckp) {
			error = union_dircheckp(td, &vp, fp);
			if (error == -1)
				goto unionread;
			if (error)
				return (error);
		}
		if ((vp->v_flag & VROOT) &&
		    (vp->v_mount->mnt_flag & MNT_UNION)) {
			struct vnode *tvp = vp;
			vp = vp->v_mount->mnt_vnodecovered;
			vref(vp);
			fp->f_data = (caddr_t)vp;
			fp->f_offset = 0;
			vrele(tvp);
			goto unionread;
		}
	}
	if (basep) {
		*basep = loff;
	}
	*res = count - auio.uio_resid;
	return (error);
}

/*
 * getdirentries_args(int fd, char *buf, u_int conut, long *basep)
 *
 * Read a block of directory entries in a file system independent format.
 */
int
getdirentries(struct getdirentries_args *uap)
{
	long base;
	int error;

	error = kern_getdirentries(uap->fd, uap->buf, uap->count, &base,
	    &uap->sysmsg_result);

	if (error == 0)
		error = copyout(&base, uap->basep, sizeof(*uap->basep));
	return (error);
}

/*
 * getdents_args(int fd, char *buf, size_t count)
 */
int
getdents(struct getdents_args *uap)
{
	int error;

	error = kern_getdirentries(uap->fd, uap->buf, uap->count, NULL,
	    &uap->sysmsg_result);

	return (error);
}

/*
 * umask(int newmask)
 *
 * Set the mode mask for creation of filesystem nodes.
 *
 * MP SAFE
 */
int
umask(struct umask_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp;

	fdp = p->p_fd;
	uap->sysmsg_result = fdp->fd_cmask;
	fdp->fd_cmask = SCARG(uap, newmask) & ALLPERMS;
	return (0);
}

/*
 * revoke(char *path)
 *
 * Void all references to file by ripping underlying filesystem
 * away from vnode.
 */
/* ARGSUSED */
int
revoke(struct revoke_args *uap)
{
	struct thread *td = curthread;
	struct nlookupdata nd;
	struct vattr vattr;
	struct vnode *vp;
	struct ucred *cred;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, SCARG(uap, path), UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(nd.nl_ncp, nd.nl_cred, &vp);
	cred = crhold(nd.nl_cred);
	nlookup_done(&nd);
	if (error == 0) {
		if (vp->v_type != VCHR && vp->v_type != VBLK)
			error = EINVAL;
		if (error == 0)
			error = VOP_GETATTR(vp, &vattr, td);
		if (error == 0 && cred->cr_uid != vattr.va_uid)
			error = suser_cred(cred, PRISON_ROOT);
		if (error == 0 && count_udev(vp->v_udev) > 0) {
			if ((error = vx_lock(vp)) == 0) {
				VOP_REVOKE(vp, REVOKEALL);
				vx_unlock(vp);
			}
		}
		vrele(vp);
	}
	crfree(cred);
	return (error);
}

/*
 * Convert a user file descriptor to a kernel file entry.
 */
int
getvnode(struct filedesc *fdp, int fd, struct file **fpp)
{
	struct file *fp;

	if ((u_int)fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_ofiles[fd]) == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_VNODE && fp->f_type != DTYPE_FIFO)
		return (EINVAL);
	*fpp = fp;
	return (0);
}
/*
 * getfh_args(char *fname, fhandle_t *fhp)
 *
 * Get (NFS) file handle
 */
int
getfh(struct getfh_args *uap)
{
	struct thread *td = curthread;
	struct nlookupdata nd;
	fhandle_t fh;
	struct vnode *vp;
	int error;

	/*
	 * Must be super user
	 */
	if ((error = suser(td)) != 0)
		return (error);

	vp = NULL;
	error = nlookup_init(&nd, uap->fname, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(nd.nl_ncp, nd.nl_cred, LK_EXCLUSIVE, &vp);
	nlookup_done(&nd);
	if (error == 0) {
		bzero(&fh, sizeof(fh));
		fh.fh_fsid = vp->v_mount->mnt_stat.f_fsid;
		error = VFS_VPTOFH(vp, &fh.fh_fid);
		vput(vp);
		if (error == 0)
			error = copyout(&fh, uap->fhp, sizeof(fh));
	}
	return (error);
}

/*
 * fhopen_args(const struct fhandle *u_fhp, int flags)
 *
 * syscall for the rpc.lockd to use to translate a NFS file handle into
 * an open descriptor.
 *
 * warning: do not remove the suser() call or this becomes one giant
 * security hole.
 */
int
fhopen(struct fhopen_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct mount *mp;
	struct vnode *vp;
	struct fhandle fhp;
	struct vattr vat;
	struct vattr *vap = &vat;
	struct flock lf;
	struct filedesc *fdp = p->p_fd;
	int fmode, mode, error, type;
	struct file *nfp; 
	struct file *fp;
	int indx;

	/*
	 * Must be super user
	 */
	error = suser(td);
	if (error)
		return (error);

	fmode = FFLAGS(SCARG(uap, flags));
	/* why not allow a non-read/write open for our lockd? */
	if (((fmode & (FREAD | FWRITE)) == 0) || (fmode & O_CREAT))
		return (EINVAL);
	error = copyin(SCARG(uap,u_fhp), &fhp, sizeof(fhp));
	if (error)
		return(error);
	/* find the mount point */
	mp = vfs_getvfs(&fhp.fh_fsid);
	if (mp == NULL)
		return (ESTALE);
	/* now give me my vnode, it gets returned to me locked */
	error = VFS_FHTOVP(mp, &fhp.fh_fid, &vp);
	if (error)
		return (error);
 	/*
	 * from now on we have to make sure not
	 * to forget about the vnode
	 * any error that causes an abort must vput(vp) 
	 * just set error = err and 'goto bad;'.
	 */

	/* 
	 * from vn_open 
	 */
	if (vp->v_type == VLNK) {
		error = EMLINK;
		goto bad;
	}
	if (vp->v_type == VSOCK) {
		error = EOPNOTSUPP;
		goto bad;
	}
	mode = 0;
	if (fmode & (FWRITE | O_TRUNC)) {
		if (vp->v_type == VDIR) {
			error = EISDIR;
			goto bad;
		}
		error = vn_writechk(vp);
		if (error)
			goto bad;
		mode |= VWRITE;
	}
	if (fmode & FREAD)
		mode |= VREAD;
	if (mode) {
		error = VOP_ACCESS(vp, mode, p->p_ucred, td);
		if (error)
			goto bad;
	}
	if (fmode & O_TRUNC) {
		VOP_UNLOCK(vp, 0, td);			/* XXX */
		VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);	/* XXX */
		VATTR_NULL(vap);
		vap->va_size = 0;
		error = VOP_SETATTR(vp, vap, p->p_ucred, td);
		if (error)
			goto bad;
	}

	/*
	 * VOP_OPEN needs the file pointer so it can potentially override
	 * it.
	 *
	 * WARNING! no f_ncp will be associated when fhopen()ing a directory.
	 * XXX
	 */
	if ((error = falloc(p, &nfp, NULL)) != 0)
		goto bad;
	fp = nfp;

	fp->f_data = (caddr_t)vp;
	fp->f_flag = fmode & FMASK;
	fp->f_ops = &vnode_fileops;
	fp->f_type = DTYPE_VNODE;

	error = VOP_OPEN(vp, fmode, p->p_ucred, fp, td);
	if (error) {
		/*
		 * setting f_ops this way prevents VOP_CLOSE from being
		 * called or fdrop() releasing the vp from v_data.   Since
		 * the VOP_OPEN failed we don't want to VOP_CLOSE.
		 */
		fp->f_ops = &badfileops;
		fp->f_data = NULL;
		fdrop(fp, td);
		goto bad;
	}
	if (fmode & FWRITE)
		vp->v_writecount++;

	/*
	 * The fp now owns a reference on the vnode.  We still have our own
	 * ref+lock.
	 */
	vref(vp);

	/*
	 * Make sure that a VM object is created for VMIO support.  If this
	 * fails just fdrop() normally to clean up.
	 */
	if (vn_canvmio(vp) == TRUE) {
		if ((error = vfs_object_create(vp, td)) != 0) {
			fdrop(fp, td);
			goto bad;
		}
	}

	/*
	 * The open was successful, associate it with a file descriptor.
	 */
	if ((error = fsetfd(p, fp, &indx)) != 0) {
		if (fmode & FWRITE)
			vp->v_writecount--;
		fdrop(fp, td);
		goto bad;
	}

	if (fmode & (O_EXLOCK | O_SHLOCK)) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		if (fmode & O_EXLOCK)
			lf.l_type = F_WRLCK;
		else
			lf.l_type = F_RDLCK;
		type = F_FLOCK;
		if ((fmode & FNONBLOCK) == 0)
			type |= F_WAIT;
		VOP_UNLOCK(vp, 0, td);
		if ((error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, type)) != 0) {
			/*
			 * lock request failed.  Normally close the descriptor
			 * but handle the case where someone might have dup()d
			 * or close()d it when we weren't looking.
			 */
			if (fdp->fd_ofiles[indx] == fp) {
				fdp->fd_ofiles[indx] = NULL;
				fdrop(fp, td);
			}

			/*
			 * release our private reference.
			 */
			fdrop(fp, td);
			vrele(vp);
			return (error);
		}
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
		fp->f_flag |= FHASLOCK;
	}
	if ((vp->v_type == VREG) && (VOP_GETVOBJECT(vp, NULL) != 0))
		vfs_object_create(vp, td);

	vput(vp);
	fdrop(fp, td);
	uap->sysmsg_result = indx;
	return (0);

bad:
	vput(vp);
	return (error);
}

/*
 * fhstat_args(struct fhandle *u_fhp, struct stat *sb)
 */
int
fhstat(struct fhstat_args *uap)
{
	struct thread *td = curthread;
	struct stat sb;
	fhandle_t fh;
	struct mount *mp;
	struct vnode *vp;
	int error;

	/*
	 * Must be super user
	 */
	error = suser(td);
	if (error)
		return (error);
	
	error = copyin(SCARG(uap, u_fhp), &fh, sizeof(fhandle_t));
	if (error)
		return (error);

	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL)
		return (ESTALE);
	if ((error = VFS_FHTOVP(mp, &fh.fh_fid, &vp)))
		return (error);
	error = vn_stat(vp, &sb, td);
	vput(vp);
	if (error)
		return (error);
	error = copyout(&sb, SCARG(uap, sb), sizeof(sb));
	return (error);
}

/*
 * fhstatfs_args(struct fhandle *u_fhp, struct statfs *buf)
 */
int
fhstatfs(struct fhstatfs_args *uap)
{
	struct thread *td = curthread;
	struct statfs *sp;
	struct mount *mp;
	struct vnode *vp;
	struct statfs sb;
	fhandle_t fh;
	int error;

	/*
	 * Must be super user
	 */
	if ((error = suser(td)))
		return (error);

	if ((error = copyin(SCARG(uap, u_fhp), &fh, sizeof(fhandle_t))) != 0)
		return (error);

	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL)
		return (ESTALE);
	if ((error = VFS_FHTOVP(mp, &fh.fh_fid, &vp)))
		return (error);
	mp = vp->v_mount;
	sp = &mp->mnt_stat;
	vput(vp);
	if ((error = VFS_STATFS(mp, sp, td)) != 0)
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	if (suser(td)) {
		bcopy(sp, &sb, sizeof(sb));
		sb.f_fsid.val[0] = sb.f_fsid.val[1] = 0;
		sp = &sb;
	}
	return (copyout(sp, SCARG(uap, buf), sizeof(*sp)));
}

/*
 * Syscall to push extended attribute configuration information into the
 * VFS.  Accepts a path, which it converts to a mountpoint, as well as
 * a command (int cmd), and attribute name and misc data.  For now, the
 * attribute name is left in userspace for consumption by the VFS_op.
 * It will probably be changed to be copied into sysspace by the
 * syscall in the future, once issues with various consumers of the
 * attribute code have raised their hands.
 *
 * Currently this is used only by UFS Extended Attributes.
 */
int
extattrctl(struct extattrctl_args *uap)
{
	struct nlookupdata nd;
	struct mount *mp;
	struct vnode *vp;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0) {
		mp = nd.nl_ncp->nc_mount;
		error = VFS_EXTATTRCTL(mp, SCARG(uap, cmd), 
				SCARG(uap, attrname), SCARG(uap, arg), 
				nd.nl_td);
	}
	nlookup_done(&nd);
	return (error);
}

/*
 * Syscall to set a named extended attribute on a file or directory.
 * Accepts attribute name, and a uio structure pointing to the data to set.
 * The uio is consumed in the style of writev().  The real work happens
 * in VOP_SETEXTATTR().
 */
int
extattr_set_file(struct extattr_set_file_args *uap)
{
	char attrname[EXTATTR_MAXNAMELEN];
	struct iovec aiov[UIO_SMALLIOV];
	struct iovec *needfree;
	struct nlookupdata nd;
	struct iovec *iov;
	struct vnode *vp;
	struct uio auio;
	u_int iovlen;
	u_int cnt;
	int error;
	int i;

	error = copyin(SCARG(uap, attrname), attrname, EXTATTR_MAXNAMELEN);
	if (error)
		return (error);

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(nd.nl_ncp, nd.nl_cred, LK_EXCLUSIVE, &vp);
	if (error) {
		nlookup_done(&nd);
		return (error);
	}

	needfree = NULL;
	iovlen = uap->iovcnt * sizeof(struct iovec);
	if (uap->iovcnt > UIO_SMALLIOV) {
		if (uap->iovcnt > UIO_MAXIOV) {
			error = EINVAL;
			goto done;
		}
		MALLOC(iov, struct iovec *, iovlen, M_IOV, M_WAITOK);
		needfree = iov;
	} else {
		iov = aiov;
	}
	auio.uio_iov = iov;
	auio.uio_iovcnt = uap->iovcnt;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = nd.nl_td;
	auio.uio_offset = 0;
	if ((error = copyin(uap->iovp, iov, iovlen)))
		goto done;
	auio.uio_resid = 0;
	for (i = 0; i < uap->iovcnt; i++) {
		if (iov->iov_len > INT_MAX - auio.uio_resid) {
			error = EINVAL;
			goto done;
		}
		auio.uio_resid += iov->iov_len;
		iov++;
	}
	cnt = auio.uio_resid;
	error = VOP_SETEXTATTR(vp, attrname, &auio, nd.nl_cred, nd.nl_td);
	cnt -= auio.uio_resid;
	uap->sysmsg_result = cnt;
done:
	vput(vp);
	nlookup_done(&nd);
	if (needfree)
		FREE(needfree, M_IOV);
	return (error);
}

/*
 * Syscall to get a named extended attribute on a file or directory.
 * Accepts attribute name, and a uio structure pointing to a buffer for the
 * data.  The uio is consumed in the style of readv().  The real work
 * happens in VOP_GETEXTATTR();
 */
int
extattr_get_file(struct extattr_get_file_args *uap)
{
	char attrname[EXTATTR_MAXNAMELEN];
	struct iovec aiov[UIO_SMALLIOV];
	struct iovec *needfree;
	struct nlookupdata nd;
	struct iovec *iov;
	struct vnode *vp;
	struct uio auio;
	u_int iovlen;
	u_int cnt;
	int error;
	int i;

	error = copyin(SCARG(uap, attrname), attrname, EXTATTR_MAXNAMELEN);
	if (error)
		return (error);

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(nd.nl_ncp, nd.nl_cred, LK_EXCLUSIVE, &vp);
	if (error) {
		nlookup_done(&nd);
		return (error);
	}

	iovlen = uap->iovcnt * sizeof (struct iovec);
	needfree = NULL;
	if (uap->iovcnt > UIO_SMALLIOV) {
		if (uap->iovcnt > UIO_MAXIOV) {
			error = EINVAL;
			goto done;
		}
		MALLOC(iov, struct iovec *, iovlen, M_IOV, M_WAITOK);
		needfree = iov;
	} else {
		iov = aiov;
	}
	auio.uio_iov = iov;
	auio.uio_iovcnt = uap->iovcnt;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = nd.nl_td;
	auio.uio_offset = 0;
	if ((error = copyin(uap->iovp, iov, iovlen)))
		goto done;
	auio.uio_resid = 0;
	for (i = 0; i < uap->iovcnt; i++) {
		if (iov->iov_len > INT_MAX - auio.uio_resid) {
			error = EINVAL;
			goto done;
		}
		auio.uio_resid += iov->iov_len;
		iov++;
	}
	cnt = auio.uio_resid;
	error = VOP_GETEXTATTR(vp, attrname, &auio, nd.nl_cred, nd.nl_td);
	cnt -= auio.uio_resid;
	uap->sysmsg_result = cnt;
done:
	vput(vp);
	nlookup_done(&nd);
	if (needfree)
		FREE(needfree, M_IOV);
	return(error);
}

/*
 * Syscall to delete a named extended attribute from a file or directory.
 * Accepts attribute name.  The real work happens in VOP_SETEXTATTR().
 */
int
extattr_delete_file(struct extattr_delete_file_args *uap)
{
	char attrname[EXTATTR_MAXNAMELEN];
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	error = copyin(SCARG(uap, attrname), attrname, EXTATTR_MAXNAMELEN);
	if (error)
		return(error);

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(nd.nl_ncp, nd.nl_cred, LK_EXCLUSIVE, &vp);
	if (error) {
		nlookup_done(&nd);
		return (error);
	}

	error = VOP_SETEXTATTR(vp, attrname, NULL, nd.nl_cred, nd.nl_td);
	vput(vp);
	nlookup_done(&nd);
	return(error);
}

/*
 * print out statistics from the current status of the buffer pool
 * this can be toggeled by the system control option debug.syncprt
 */
#ifdef DEBUG
void
vfs_bufstats(void)
{
        int s, i, j, count;
        struct buf *bp;
        struct bqueues *dp;
        int counts[(MAXBSIZE / PAGE_SIZE) + 1];
        static char *bname[3] = { "LOCKED", "LRU", "AGE" };

        for (dp = bufqueues, i = 0; dp < &bufqueues[3]; dp++, i++) {
                count = 0;
                for (j = 0; j <= MAXBSIZE/PAGE_SIZE; j++)
                        counts[j] = 0;
                s = splbio();
                TAILQ_FOREACH(bp, dp, b_freelist) {
                        counts[bp->b_bufsize/PAGE_SIZE]++;
                        count++;
                }
                splx(s);
                printf("%s: total-%d", bname[i], count);
                for (j = 0; j <= MAXBSIZE/PAGE_SIZE; j++)
                        if (counts[j] != 0)
                                printf(", %d-%d", j * PAGE_SIZE, counts[j]);
                printf("\n");
        }
}
#endif
