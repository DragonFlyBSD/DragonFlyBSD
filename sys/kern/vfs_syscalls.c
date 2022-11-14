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
 *	@(#)vfs_syscalls.c	8.13 (Berkeley) 4/15/94
 * $FreeBSD: src/sys/kern/vfs_syscalls.c,v 1.151.2.18 2003/04/04 20:35:58 tegge Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/sysent.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mountctl.h>
#include <sys/sysmsg.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/linker.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/jail.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/dirent.h>
#include <sys/extattr.h>
#include <sys/spinlock.h>
#include <sys/kern_syscall.h>
#include <sys/objcache.h>
#include <sys/sysctl.h>

#include <sys/buf2.h>
#include <sys/file2.h>
#include <sys/spinlock2.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>

#include <machine/limits.h>
#include <machine/stdarg.h>

static void mount_warning(struct mount *mp, const char *ctl, ...)
		__printflike(2, 3);
static int mount_path(struct proc *p, struct mount *mp, char **rb, char **fb);
static int checkvp_chdir (struct vnode *vn, struct thread *td);
static void checkdirs (struct nchandle *old_nch, struct nchandle *new_nch);
static int get_fspriv(const char *);
static int chroot_refuse_vdir_fds (thread_t td, struct filedesc *fdp);
static int chroot_visible_mnt(struct mount *mp, struct proc *p);
static int getutimes (struct timeval *, struct timespec *);
static int getutimens (const struct timespec *, struct timespec *, int *);
static int setfown (struct mount *, struct vnode *, uid_t, gid_t);
static int setfmode (struct vnode *, int);
static int setfflags (struct vnode *, u_long);
static int setutimes (struct vnode *, struct vattr *,
			const struct timespec *, int);

static int	usermount = 0;	/* if 1, non-root can mount fs. */
SYSCTL_INT(_vfs, OID_AUTO, usermount, CTLFLAG_RW, &usermount, 0,
    "Allow non-root users to mount filesystems");

static int	debug_unmount = 0; /* if 1 loop until unmount success */
SYSCTL_INT(_vfs, OID_AUTO, debug_unmount, CTLFLAG_RW, &debug_unmount, 0,
    "Stall failed unmounts in loop");

static struct krate krate_rename = { 1 };

/*
 * Virtual File System System Calls
 */

/*
 * Mount a file system.
 *
 * mount_args(char *type, char *path, int flags, caddr_t data)
 *
 * MPALMOSTSAFE
 */
int
sys_mount(struct sysmsg *sysmsg, const struct mount_args *uap)
{
	struct thread *td = curthread;
	struct vnode *vp;
	struct nchandle nch;
	struct mount *mp, *nullmp;
	struct vfsconf *vfsp;
	int error, flag = 0, flag2 = 0;
	int hasmount;
	int priv = 0;
	int flags = uap->flags;
	struct vattr va;
	struct nlookupdata nd;
	char fstypename[MFSNAMELEN];
	struct ucred *cred;

	cred = td->td_ucred;

	/* We do not allow user mounts inside a jail for now */
	if (usermount && jailed(cred)) {
		error = EPERM;
		goto done;
	}

	/*
	 * Extract the file system type. We need to know this early, to take
	 * appropriate actions for jails and nullfs mounts.
	 */
        if ((error = copyinstr(uap->type, fstypename, MFSNAMELEN, NULL)) != 0)
		goto done;

	/*
	 * Select the correct priv according to the file system type.
	 */
	priv = get_fspriv(fstypename);

	if (usermount == 0 && (error = priv_check(td, priv)))
		goto done;

	/*
	 * Do not allow NFS export by non-root users.
	 */
	if (flags & MNT_EXPORTED) {
		error = priv_check(td, priv);
		if (error)
			goto done;
	}
	/*
	 * Silently enforce MNT_NOSUID and MNT_NODEV for non-root users
	 */
	if (priv_check(td, priv))
		flags |= MNT_NOSUID | MNT_NODEV;

	/*
	 * Lookup the requested path and extract the nch and vnode.
	 */
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0) {
		if ((error = nlookup(&nd)) == 0) {
			if (nd.nl_nch.ncp->nc_vp == NULL)
				error = ENOENT;
		}
	}
	if (error) {
		nlookup_done(&nd);
		goto done;
	}

	/*
	 * If the target filesystem is resolved via a nullfs mount, then
	 * nd.nl_nch.mount will be pointing to the nullfs mount structure
	 * instead of the target file system. We need it in case we are
	 * doing an update.
	 */
	nullmp = nd.nl_nch.mount;

	/*
	 * Extract the locked+refd ncp and cleanup the nd structure
	 */
	nch = nd.nl_nch;
	cache_zero(&nd.nl_nch);
	nlookup_done(&nd);

	if ((nch.ncp->nc_flag & NCF_ISMOUNTPT) &&
	    (mp = cache_findmount(&nch)) != NULL) {
		cache_dropmount(mp);
		hasmount = 1;
	} else {
		hasmount = 0;
	}


	/*
	 * now we have the locked ref'd nch and unreferenced vnode.
	 */
	vp = nch.ncp->nc_vp;
	if ((error = vget(vp, LK_EXCLUSIVE)) != 0) {
		cache_put(&nch);
		goto done;
	}
	cache_unlock(&nch);

	/*
	 * Now we have an unlocked ref'd nch and a locked ref'd vp
	 */
	if (flags & MNT_UPDATE) {
		if ((vp->v_flag & (VROOT|VPFSROOT)) == 0) {
			cache_drop(&nch);
			vput(vp);
			error = EINVAL;
			goto done;
		}

		if (strncmp(fstypename, "null", 5) == 0) {
			KKASSERT(nullmp);
			mp = nullmp;
		} else {
			mp = vp->v_mount;
		}

		flag = mp->mnt_flag;
		flag2 = mp->mnt_kern_flag;
		/*
		 * We only allow the filesystem to be reloaded if it
		 * is currently mounted read-only.
		 */
		if ((flags & MNT_RELOAD) &&
		    ((mp->mnt_flag & MNT_RDONLY) == 0)) {
			cache_drop(&nch);
			vput(vp);
			error = EOPNOTSUPP;	/* Needs translation */
			goto done;
		}
		/*
		 * Only root, or the user that did the original mount is
		 * permitted to update it.
		 */
		if (mp->mnt_stat.f_owner != cred->cr_uid &&
		    (error = priv_check(td, priv))) {
			cache_drop(&nch);
			vput(vp);
			goto done;
		}
		if (vfs_busy(mp, LK_NOWAIT)) {
			cache_drop(&nch);
			vput(vp);
			error = EBUSY;
			goto done;
		}
		if (hasmount) {
			cache_drop(&nch);
			vfs_unbusy(mp);
			vput(vp);
			error = EBUSY;
			goto done;
		}
		mp->mnt_flag |= flags & (MNT_RELOAD | MNT_FORCE | MNT_UPDATE);
		lwkt_gettoken(&mp->mnt_token);
		vn_unlock(vp);
		vfsp = mp->mnt_vfc;
		goto update;
	}

	/*
	 * If the user is not root, ensure that they own the directory
	 * onto which we are attempting to mount.
	 */
	if ((error = VOP_GETATTR(vp, &va)) ||
	    (va.va_uid != cred->cr_uid &&
	     (error = priv_check(td, priv)))) {
		cache_drop(&nch);
		vput(vp);
		goto done;
	}
	if ((error = vinvalbuf(vp, V_SAVE, 0, 0)) != 0) {
		cache_drop(&nch);
		vput(vp);
		goto done;
	}
	if (vp->v_type != VDIR) {
		cache_drop(&nch);
		vput(vp);
		error = ENOTDIR;
		goto done;
	}
	if (vp->v_mount->mnt_kern_flag & MNTK_NOSTKMNT) {
		cache_drop(&nch);
		vput(vp);
		error = EPERM;
		goto done;
	}
	vfsp = vfsconf_find_by_name(fstypename);
	if (vfsp == NULL) {
		linker_file_t lf;

		/* Only load modules for root (very important!) */
		if ((error = priv_check(td, PRIV_ROOT)) != 0) {
			cache_drop(&nch);
			vput(vp);
			goto done;
		}
		error = linker_load_file(fstypename, &lf);
		if (error || lf == NULL) {
			cache_drop(&nch);
			vput(vp);
			if (lf == NULL)
				error = ENODEV;
			goto done;
		}
		lf->userrefs++;
		/* lookup again, see if the VFS was loaded */
		vfsp = vfsconf_find_by_name(fstypename);
		if (vfsp == NULL) {
			lf->userrefs--;
			linker_file_unload(lf);
			cache_drop(&nch);
			vput(vp);
			error = ENODEV;
			goto done;
		}
	}
	if (hasmount) {
		cache_drop(&nch);
		vput(vp);
		error = EBUSY;
		goto done;
	}

	/*
	 * Allocate and initialize the filesystem.
	 */
	mp = kmalloc(sizeof(struct mount), M_MOUNT, M_ZERO|M_WAITOK);
	mount_init(mp, vfsp->vfc_vfsops);
	vfs_busy(mp, LK_NOWAIT);
	mp->mnt_vfc = vfsp;
	mp->mnt_pbuf_count = nswbuf_kva / NSWBUF_SPLIT;
	vfsp->vfc_refcount++;
	mp->mnt_stat.f_type = vfsp->vfc_typenum;
	mp->mnt_flag |= vfsp->vfc_flags & MNT_VISFLAGMASK;
	strncpy(mp->mnt_stat.f_fstypename, vfsp->vfc_name, MFSNAMELEN);
	mp->mnt_stat.f_owner = cred->cr_uid;
	lwkt_gettoken(&mp->mnt_token);
	vn_unlock(vp);
update:
	/*
	 * (per-mount token acquired at this point)
	 *
	 * Set the mount level flags.
	 */
	if (flags & MNT_RDONLY)
		mp->mnt_flag |= MNT_RDONLY;
	else if (mp->mnt_flag & MNT_RDONLY)
		mp->mnt_kern_flag |= MNTK_WANTRDWR;
	mp->mnt_flag &=~ (MNT_NOSUID | MNT_NOEXEC | MNT_NODEV |
	    MNT_SYNCHRONOUS | MNT_ASYNC | MNT_NOATIME |
	    MNT_NOSYMFOLLOW | MNT_IGNORE | MNT_TRIM |
	    MNT_NOCLUSTERR | MNT_NOCLUSTERW | MNT_SUIDDIR |
	    MNT_AUTOMOUNTED);
	mp->mnt_flag |= flags & (MNT_NOSUID | MNT_NOEXEC |
	    MNT_NODEV | MNT_SYNCHRONOUS | MNT_ASYNC | MNT_FORCE |
	    MNT_NOSYMFOLLOW | MNT_IGNORE | MNT_TRIM |
	    MNT_NOATIME | MNT_NOCLUSTERR | MNT_NOCLUSTERW | MNT_SUIDDIR |
	    MNT_AUTOMOUNTED);

	/*
	 * Pre-set the mount's ALL_MPSAFE flags if specified in the vfsconf.
	 * This way the initial VFS_MOUNT() call will also be MPSAFE.
	 */
	if (vfsp->vfc_flags & VFCF_MPSAFE)
		mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;

	/*
	 * Mount the filesystem.
	 * XXX The final recipients of VFS_MOUNT just overwrite the ndp they
	 * get.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		error = VFS_MOUNT(mp, uap->path, uap->data, cred);
		if (mp->mnt_kern_flag & MNTK_WANTRDWR)
			mp->mnt_flag &= ~MNT_RDONLY;
		mp->mnt_flag &=~ (MNT_UPDATE | MNT_RELOAD | MNT_FORCE);
		mp->mnt_kern_flag &=~ MNTK_WANTRDWR;
		if (error) {
			mp->mnt_flag = flag;
			mp->mnt_kern_flag = flag2;
		}
		lwkt_reltoken(&mp->mnt_token);
		vfs_unbusy(mp);
		vrele(vp);
		cache_drop(&nch);
		goto done;
	}
	mp->mnt_ncmounton = nch;
	error = VFS_MOUNT(mp, uap->path, uap->data, cred);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	/*
	 * Put the new filesystem on the mount list after root.  The mount
	 * point gets its own mnt_ncmountpt (unless the VFS already set one
	 * up) which represents the root of the mount.  The lookup code
	 * detects the mount point going forward and checks the root of
	 * the mount going backwards.
	 *
	 * It is not necessary to invalidate or purge the vnode underneath
	 * because elements under the mount will be given their own glue
	 * namecache record.
	 */
	if (!error) {
		if (mp->mnt_ncmountpt.ncp == NULL) {
			/* 
			 * Allocate, then unlock, but leave the ref intact.
			 * This is the mnt_refs (1) that we will retain
			 * through to the unmount.
			 */
			cache_allocroot(&mp->mnt_ncmountpt, mp, NULL);
			cache_unlock(&mp->mnt_ncmountpt);
		}
		vn_unlock(vp);
		cache_lock(&nch);
		nch.ncp->nc_flag |= NCF_ISMOUNTPT;
		cache_unlock(&nch);
		cache_ismounting(mp);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

		mountlist_insert(mp, MNTINS_LAST);
		vn_unlock(vp);
		checkdirs(&mp->mnt_ncmounton, &mp->mnt_ncmountpt);
		error = vfs_allocate_syncvnode(mp);
		lwkt_reltoken(&mp->mnt_token);
		vfs_unbusy(mp);
		error = VFS_START(mp, 0);
		vrele(vp);
		KNOTE(&fs_klist, VQ_MOUNT);
	} else {
		bzero(&mp->mnt_ncmounton, sizeof(mp->mnt_ncmounton));
		vn_syncer_thr_stop(mp);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_coherency_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_journal_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_norm_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_spec_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_fifo_ops);
		if (mp->mnt_cred) {
			crfree(mp->mnt_cred);
			mp->mnt_cred = NULL;
		}
		mp->mnt_vfc->vfc_refcount--;
		lwkt_reltoken(&mp->mnt_token);
		vfs_unbusy(mp);
		kfree(mp, M_MOUNT);
		cache_drop(&nch);
		vput(vp);
	}
done:
	return (error);
}

/*
 * Scan all active processes to see if any of them have a current
 * or root directory onto which the new filesystem has just been
 * mounted. If so, replace them with the new mount point.
 *
 * Both old_nch and new_nch are ref'd on call but not locked.
 * new_nch must be temporarily locked so it can be associated with the
 * vnode representing the root of the mount point.
 */
struct checkdirs_info {
	struct nchandle old_nch;
	struct nchandle new_nch;
	struct vnode *old_vp;
	struct vnode *new_vp;
};

static int checkdirs_callback(struct proc *p, void *data);

static void
checkdirs(struct nchandle *old_nch, struct nchandle *new_nch)
{
	struct checkdirs_info info;
	struct vnode *olddp;
	struct vnode *newdp;
	struct mount *mp;

	/*
	 * If the old mount point's vnode has a usecount of 1, it is not
	 * being held as a descriptor anywhere.
	 */
	olddp = old_nch->ncp->nc_vp;
	if (olddp == NULL || VREFCNT(olddp) == 1)
		return;

	/*
	 * Force the root vnode of the new mount point to be resolved
	 * so we can update any matching processes.
	 */
	mp = new_nch->mount;
	if (VFS_ROOT(mp, &newdp))
		panic("mount: lost mount");
	vn_unlock(newdp);
	cache_lock(new_nch);
	vn_lock(newdp, LK_EXCLUSIVE | LK_RETRY);
	cache_setunresolved(new_nch);
	cache_setvp(new_nch, newdp);
	cache_unlock(new_nch);

	/*
	 * Special handling of the root node
	 */
	if (rootvnode == olddp) {
		vref(newdp);
		vfs_cache_setroot(newdp, cache_hold(new_nch));
	}

	/*
	 * Pass newdp separately so the callback does not have to access
	 * it via new_nch->ncp->nc_vp.
	 */
	info.old_nch = *old_nch;
	info.new_nch = *new_nch;
	info.new_vp = newdp;
	allproc_scan(checkdirs_callback, &info, 0);
	vput(newdp);
}

/*
 * NOTE: callback is not MP safe because the scanned process's filedesc
 * structure can be ripped out from under us, amoung other things.
 */
static int
checkdirs_callback(struct proc *p, void *data)
{
	struct checkdirs_info *info = data;
	struct filedesc *fdp;
	struct nchandle ncdrop1;
	struct nchandle ncdrop2;
	struct vnode *vprele1;
	struct vnode *vprele2;

	if ((fdp = p->p_fd) != NULL) {
		cache_zero(&ncdrop1);
		cache_zero(&ncdrop2);
		vprele1 = NULL;
		vprele2 = NULL;

		/*
		 * MPUNSAFE - XXX fdp can be pulled out from under a
		 * foreign process.
		 *
		 * A shared filedesc is ok, we don't have to copy it
		 * because we are making this change globally.
		 */
		spin_lock(&fdp->fd_spin);
		if (fdp->fd_ncdir.mount == info->old_nch.mount &&
		    fdp->fd_ncdir.ncp == info->old_nch.ncp) {
			vprele1 = fdp->fd_cdir;
			vref(info->new_vp);
			fdp->fd_cdir = info->new_vp;
			ncdrop1 = fdp->fd_ncdir;
			cache_copy(&info->new_nch, &fdp->fd_ncdir);
		}
		if (fdp->fd_nrdir.mount == info->old_nch.mount &&
		    fdp->fd_nrdir.ncp == info->old_nch.ncp) {
			vprele2 = fdp->fd_rdir;
			vref(info->new_vp);
			fdp->fd_rdir = info->new_vp;
			ncdrop2 = fdp->fd_nrdir;
			cache_copy(&info->new_nch, &fdp->fd_nrdir);
		}
		spin_unlock(&fdp->fd_spin);
		if (ncdrop1.ncp)
			cache_drop(&ncdrop1);
		if (ncdrop2.ncp)
			cache_drop(&ncdrop2);
		if (vprele1)
			vrele(vprele1);
		if (vprele2)
			vrele(vprele2);
	}
	return(0);
}

/*
 * Unmount a file system.
 *
 * Note: unmount takes a path to the vnode mounted on as argument,
 * not special file (as before).
 *
 * umount_args(char *path, int flags)
 *
 * MPALMOSTSAFE
 */
int
sys_unmount(struct sysmsg *sysmsg, const struct unmount_args *uap)
{
	struct thread *td = curthread;
	struct proc *p __debugvar = td->td_proc;
	struct mount *mp = NULL;
	struct nlookupdata nd;
	char fstypename[MFSNAMELEN];
	int priv = 0;
	int error;
	struct ucred *cred;

	cred = td->td_ucred;

	KKASSERT(p);

	/* We do not allow user umounts inside a jail for now */
	if (usermount && jailed(cred)) {
		error = EPERM;
		goto done;
	}

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE,
			     NLC_FOLLOW | NLC_IGNBADDIR);
	if (error == 0)
		error = nlookup(&nd);
	if (error)
		goto out;

	mp = nd.nl_nch.mount;

	/* Figure out the fsname in order to select proper privs */
	ksnprintf(fstypename, MFSNAMELEN, "%s", mp->mnt_vfc->vfc_name);
	priv = get_fspriv(fstypename);

	if (usermount == 0 && (error = priv_check(td, priv))) {
		nlookup_done(&nd);
		goto done;
	}

	/*
	 * Only root, or the user that did the original mount is
	 * permitted to unmount this filesystem.
	 */
	if ((mp->mnt_stat.f_owner != td->td_ucred->cr_uid) &&
	    (error = priv_check(td, priv)))
		goto out;

	/*
	 * Don't allow unmounting the root file system.
	 */
	if (mp->mnt_flag & MNT_ROOTFS) {
		error = EINVAL;
		goto out;
	}

	/*
	 * Must be the root of the filesystem
	 */
	if (nd.nl_nch.ncp != mp->mnt_ncmountpt.ncp) {
		error = EINVAL;
		goto out;
	}

	/* Check if this mount belongs to this prison */
	if (jailed(cred) && mp->mnt_cred && (!mp->mnt_cred->cr_prison ||
		mp->mnt_cred->cr_prison != cred->cr_prison)) {
		kprintf("mountpoint %s does not belong to this jail\n",
		    uap->path);
		error = EPERM;
		goto out;
	}

	/*
	 * If no error try to issue the unmount.  We lose our cache
	 * ref when we call nlookup_done so we must hold the mount point
	 * to prevent use-after-free races.
	 */
out:
	if (error == 0) {
		mount_hold(mp);
		nlookup_done(&nd);
		error = dounmount(mp, uap->flags, 0);
		mount_drop(mp);
	} else {
		nlookup_done(&nd);
	}
done:
	return (error);
}

/*
 * Do the actual file system unmount (interlocked against the mountlist
 * token and mp->mnt_token).
 */
static int
dounmount_interlock(struct mount *mp)
{
	if (mp->mnt_kern_flag & MNTK_UNMOUNT)
		return (EBUSY);
	mp->mnt_kern_flag |= MNTK_UNMOUNT;
	return(0);
}

static int
unmount_allproc_cb(struct proc *p, void *arg)
{
	struct mount *mp;

	if (p->p_textnch.ncp == NULL)
		return 0;

	mp = (struct mount *)arg;
	if (p->p_textnch.mount == mp)
		cache_drop(&p->p_textnch);

	return 0;
}

/*
 * The guts of the unmount code.  The mount owns one ref and one hold
 * count.  If we successfully interlock the unmount, those refs are ours.
 * (The ref is from mnt_ncmountpt).
 *
 * When halting we shortcut certain mount types such as devfs by not actually
 * issuing the VFS_SYNC() or VFS_UNMOUNT().  They are still disconnected
 * from the mountlist so higher-level filesytems can unmount cleanly.
 *
 * The mount types that allow QUICKHALT are: devfs, tmpfs, procfs.
 */
int
dounmount(struct mount *mp, int flags, int halting)
{
	struct namecache *ncp;
	struct nchandle nch;
	struct vnode *vp;
	int error;
	int async_flag;
	int lflags;
	int freeok = 1;
	int hadsyncer = 0;
	int retry;
	int quickhalt;

	lwkt_gettoken(&mp->mnt_token);

	/*
	 * When halting, certain mount points can essentially just
	 * be unhooked and otherwise ignored.
	 */
	if (halting && (mp->mnt_kern_flag & MNTK_QUICKHALT)) {
		quickhalt = 1;
		freeok = 0;
	} else {
		quickhalt = 0;
	}


	/*
	 * Exclusive access for unmounting purposes.
	 */
	if ((error = mountlist_interlock(dounmount_interlock, mp)) != 0)
		goto out;

	/*
	 * We now 'own' the last mp->mnt_refs
	 *
	 * Allow filesystems to detect that a forced unmount is in progress.
	 */
	if (flags & MNT_FORCE)
		mp->mnt_kern_flag |= MNTK_UNMOUNTF;
	lflags = LK_EXCLUSIVE | ((flags & MNT_FORCE) ? 0 : LK_TIMELOCK);
	error = lockmgr(&mp->mnt_lock, lflags);
	if (error) {
		mp->mnt_kern_flag &= ~(MNTK_UNMOUNT | MNTK_UNMOUNTF);
		if (mp->mnt_kern_flag & MNTK_MWAIT) {
			mp->mnt_kern_flag &= ~MNTK_MWAIT;
			wakeup(mp);
		}
		goto out;
	}

	if (mp->mnt_flag & MNT_EXPUBLIC)
		vfs_setpublicfs(NULL, NULL, NULL);

	vfs_msync(mp, MNT_WAIT);
	async_flag = mp->mnt_flag & MNT_ASYNC;
	mp->mnt_flag &=~ MNT_ASYNC;

	/*
	 * Decomission our special mnt_syncer vnode.  This also stops
	 * the vnlru code.  If we are unable to unmount we recommission
	 * the vnode.
	 *
	 * Then sync the filesystem.
	 */
	if ((vp = mp->mnt_syncer) != NULL) {
		mp->mnt_syncer = NULL;
		atomic_set_int(&vp->v_refcnt, VREF_FINALIZE);
		vrele(vp);
		hadsyncer = 1;
	}

	/*
	 * Sync normally-mounted filesystem.
	 */
	if (quickhalt == 0) {
		if ((mp->mnt_flag & MNT_RDONLY) == 0)
			VFS_SYNC(mp, MNT_WAIT);
	}

	/*
	 * nchandle records ref the mount structure.  Expect a count of 1
	 * (our mount->mnt_ncmountpt).
	 *
	 * Scans can get temporary refs on a mountpoint (thought really
	 * heavy duty stuff like cache_findmount() do not).
	 */
	for (retry = 0; (retry < 10 || debug_unmount); ++retry) {
		/*
		 * Invalidate the namecache topology under the mount.
		 * nullfs mounts alias a real mount's namecache topology
		 * and it should not be invalidated in that case.
		 */
		if ((mp->mnt_kern_flag & MNTK_NCALIASED) == 0) {
			cache_lock(&mp->mnt_ncmountpt);
			cache_inval(&mp->mnt_ncmountpt,
				    CINV_DESTROY|CINV_CHILDREN);
			cache_unlock(&mp->mnt_ncmountpt);
		}

		/*
		 * Clear pcpu caches
		 */
		cache_unmounting(mp);
		if (mp->mnt_refs != 1)
			cache_clearmntcache(mp);

		/*
		 * Break out if we are good.  Don't count ncp refs if the
		 * mount is aliased.
		 */
		ncp = (mp->mnt_kern_flag & MNTK_NCALIASED) ?
		      NULL : mp->mnt_ncmountpt.ncp;
		if (mp->mnt_refs == 1 &&
		    (ncp == NULL || (ncp->nc_refs == 1 &&
				     TAILQ_FIRST(&ncp->nc_list) == NULL))) {
			break;
		}

		/*
		 * If forcing the unmount, clean out any p->p_textnch
		 * nchandles that match this mount.
		 */
		if (flags & MNT_FORCE)
			allproc_scan(&unmount_allproc_cb, mp, 0);

		/*
		 * Sleep and retry.
		 */
		tsleep(&mp->mnt_refs, 0, "mntbsy", hz / 10 + 1);
		if ((retry & 15) == 15) {
			mount_warning(mp,
				      "(%p) debug - retry %d, "
				      "%d namecache refs, %d mount refs",
				      mp, retry,
				      (ncp ? ncp->nc_refs - 1 : 0),
				      mp->mnt_refs - 1);
		}
	}

	error = 0;
	ncp = (mp->mnt_kern_flag & MNTK_NCALIASED) ?
	      NULL : mp->mnt_ncmountpt.ncp;
	if (mp->mnt_refs != 1 ||
	    (ncp != NULL && (ncp->nc_refs != 1 ||
			     TAILQ_FIRST(&ncp->nc_list)))) {
		mount_warning(mp,
			      "(%p): %d namecache refs, %d mount refs "
			      "still present",
			      mp,
			      (ncp ? ncp->nc_refs - 1 : 0),
			      mp->mnt_refs - 1);
		if (flags & MNT_FORCE) {
			freeok = 0;
			mount_warning(mp, "forcing unmount\n");
		} else {
			error = EBUSY;
		}
	}

	/*
	 * So far so good, sync the filesystem once more and
	 * call the VFS unmount code if the sync succeeds.
	 */
	if (error == 0 && quickhalt == 0) {
		if (mp->mnt_flag & MNT_RDONLY) {
			error = VFS_UNMOUNT(mp, flags);
		} else {
			error = VFS_SYNC(mp, MNT_WAIT);
			if (error == 0 ||		/* no error */
			    error == EOPNOTSUPP ||	/* no sync avail */
			    (flags & MNT_FORCE)) {	/* force anyway */
				error = VFS_UNMOUNT(mp, flags);
			}
		}
		if (error) {
			mount_warning(mp,
				      "(%p) unmount: vfs refused to unmount, "
				      "error %d",
				      mp, error);
		}
	}

	/*
	 * If an error occurred we can still recover, restoring the
	 * syncer vnode and misc flags.
	 */
	if (error) {
		if (mp->mnt_syncer == NULL && hadsyncer)
			vfs_allocate_syncvnode(mp);
		mp->mnt_kern_flag &= ~(MNTK_UNMOUNT | MNTK_UNMOUNTF);
		mp->mnt_flag |= async_flag;
		lockmgr(&mp->mnt_lock, LK_RELEASE);
		if (mp->mnt_kern_flag & MNTK_MWAIT) {
			mp->mnt_kern_flag &= ~MNTK_MWAIT;
			wakeup(mp);
		}
		goto out;
	}
	/*
	 * Clean up any journals still associated with the mount after
	 * filesystem activity has ceased.
	 */
	journal_remove_all_journals(mp, 
	    ((flags & MNT_FORCE) ? MC_JOURNAL_STOP_IMM : 0));

	mountlist_remove(mp);

	/*
	 * Remove any installed vnode ops here so the individual VFSs don't
	 * have to.
	 *
	 * mnt_refs should go to zero when we scrap mnt_ncmountpt.
	 *
	 * When quickhalting we have to keep these intact because the
	 * underlying vnodes have not been destroyed, and some might be
	 * dirty.
	 */
	if (quickhalt == 0) {
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_coherency_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_journal_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_norm_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_spec_ops);
		vfs_rm_vnodeops(mp, NULL, &mp->mnt_vn_fifo_ops);
	}

	if (mp->mnt_ncmountpt.ncp != NULL) {
		nch = mp->mnt_ncmountpt;
		cache_zero(&mp->mnt_ncmountpt);
		cache_clrmountpt(&nch);
		cache_drop(&nch);
	}
	if (mp->mnt_ncmounton.ncp != NULL) {
		cache_unmounting(mp);
		nch = mp->mnt_ncmounton;
		cache_zero(&mp->mnt_ncmounton);
		cache_clrmountpt(&nch);
		cache_drop(&nch);
	}

	if (mp->mnt_cred) {
		crfree(mp->mnt_cred);
		mp->mnt_cred = NULL;
	}

	mp->mnt_vfc->vfc_refcount--;

	/*
	 * If not quickhalting the mount, we expect there to be no
	 * vnodes left.
	 */
	if (quickhalt == 0 && !TAILQ_EMPTY(&mp->mnt_nvnodelist))
		panic("unmount: dangling vnode");

	/*
	 * Release the lock
	 */
	lockmgr(&mp->mnt_lock, LK_RELEASE);
	if (mp->mnt_kern_flag & MNTK_MWAIT) {
		mp->mnt_kern_flag &= ~MNTK_MWAIT;
		wakeup(mp);
	}

	/*
	 * If we reach here and freeok != 0 we must free the mount.
	 * mnt_refs should already have dropped to 0, so if it is not
	 * zero we must cycle the caches and wait.
	 *
	 * When we are satisfied that the mount has disconnected we can
	 * drop the hold on the mp that represented the mount (though the
	 * caller might actually have another, so the caller's drop may
	 * do the actual free).
	 */
	if (freeok) {
		if (mp->mnt_refs > 0)
			cache_clearmntcache(mp);
		while (mp->mnt_refs > 0) {
			cache_unmounting(mp);
			wakeup(mp);
			tsleep(&mp->mnt_refs, 0, "umntrwait", hz / 10 + 1);
			cache_clearmntcache(mp);
		}
		lwkt_reltoken(&mp->mnt_token);
		mount_drop(mp);
		mp = NULL;
	} else {
		cache_clearmntcache(mp);
	}
	error = 0;
	KNOTE(&fs_klist, VQ_UNMOUNT);
out:
	if (mp)
		lwkt_reltoken(&mp->mnt_token);
	return (error);
}

static
void
mount_warning(struct mount *mp, const char *ctl, ...)
{
	char *ptr;
	char *buf;
	__va_list va;

	__va_start(va, ctl);
	if (cache_fullpath(NULL, &mp->mnt_ncmounton, NULL,
			   &ptr, &buf, 0) == 0) {
		kprintf("unmount(%s): ", ptr);
		kvprintf(ctl, va);
		kprintf("\n");
		kfree(buf, M_TEMP);
	} else {
		kprintf("unmount(%p", mp);
		if (mp->mnt_ncmounton.ncp && mp->mnt_ncmounton.ncp->nc_name)
			kprintf(",%s", mp->mnt_ncmounton.ncp->nc_name);
		kprintf("): ");
		kvprintf(ctl, va);
		kprintf("\n");
	}
	__va_end(va);
}

/*
 * Shim cache_fullpath() to handle the case where a process is chrooted into
 * a subdirectory of a mount.  In this case if the root mount matches the
 * process root directory's mount we have to specify the process's root
 * directory instead of the mount point, because the mount point might
 * be above the root directory.
 */
static
int
mount_path(struct proc *p, struct mount *mp, char **rb, char **fb)
{
	struct nchandle *nch;

	if (p && p->p_fd->fd_nrdir.mount == mp)
		nch = &p->p_fd->fd_nrdir;
	else
		nch = &mp->mnt_ncmountpt;
	return(cache_fullpath(p, nch, NULL, rb, fb, 0));
}

/*
 * Sync each mounted filesystem.
 */

#ifdef DEBUG
static int syncprt = 0;
SYSCTL_INT(_debug, OID_AUTO, syncprt, CTLFLAG_RW, &syncprt, 0, "");
#endif /* DEBUG */

static int sync_callback(struct mount *mp, void *data);

int
sys_sync(struct sysmsg *sysmsg, const struct sync_args *uap)
{
	mountlist_scan(sync_callback, NULL, MNTSCAN_FORWARD);
	return (0);
}

static
int
sync_callback(struct mount *mp, void *data __unused)
{
	int asyncflag;

	if ((mp->mnt_flag & MNT_RDONLY) == 0) {
		lwkt_gettoken(&mp->mnt_token);
		asyncflag = mp->mnt_flag & MNT_ASYNC;
		mp->mnt_flag &= ~MNT_ASYNC;
		lwkt_reltoken(&mp->mnt_token);
		vfs_msync(mp, MNT_NOWAIT);
		VFS_SYNC(mp, MNT_NOWAIT);
		lwkt_gettoken(&mp->mnt_token);
		mp->mnt_flag |= asyncflag;
		lwkt_reltoken(&mp->mnt_token);
	}
	return(0);
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
 *
 * MPALMOSTSAFE
 */
int
sys_quotactl(struct sysmsg *sysmsg, const struct quotactl_args *uap)
{
	struct nlookupdata nd;
	struct thread *td;
	struct mount *mp;
	int error;

	td = curthread;
	if (td->td_ucred->cr_prison && !prison_quotas) {
		error = EPERM;
		goto done;
	}

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0) {
		mp = nd.nl_nch.mount;
		error = VFS_QUOTACTL(mp, uap->cmd, uap->uid,
				    uap->arg, nd.nl_cred);
	}
	nlookup_done(&nd);
done:
	return (error);
}

/*
 * mountctl(char *path, int op, int fd, const void *ctl, int ctllen,
 *		void *buf, int buflen)
 *
 * This function operates on a mount point and executes the specified
 * operation using the specified control data, and possibly returns data.
 *
 * The actual number of bytes stored in the result buffer is returned, 0
 * if none, otherwise an error is returned.
 *
 * MPALMOSTSAFE
 */
int
sys_mountctl(struct sysmsg *sysmsg, const struct mountctl_args *uap)
{
	struct thread *td = curthread;
	struct file *fp;
	void *ctl = NULL;
	void *buf = NULL;
	char *path = NULL;
	int error;

	/*
	 * Sanity and permissions checks.  We must be root.
	 */
	if (td->td_ucred->cr_prison != NULL)
		return (EPERM);
	if ((uap->op != MOUNTCTL_MOUNTFLAGS) &&
	    (error = priv_check(td, PRIV_ROOT)) != 0)
		return (error);

	/*
	 * Argument length checks
	 */
	if (uap->ctllen < 0 || uap->ctllen > 1024)
		return (EINVAL);
	if (uap->buflen < 0 || uap->buflen > 16 * 1024)
		return (EINVAL);
	if (uap->path == NULL)
		return (EINVAL);

	/*
	 * Allocate the necessary buffers and copyin data
	 */
	path = objcache_get(namei_oc, M_WAITOK);
	error = copyinstr(uap->path, path, MAXPATHLEN, NULL);
	if (error)
		goto done;

	if (uap->ctllen) {
		ctl = kmalloc(uap->ctllen + 1, M_TEMP, M_WAITOK|M_ZERO);
		error = copyin(uap->ctl, ctl, uap->ctllen);
		if (error)
			goto done;
	}
	if (uap->buflen)
		buf = kmalloc(uap->buflen + 1, M_TEMP, M_WAITOK|M_ZERO);

	/*
	 * Validate the descriptor
	 */
	if (uap->fd >= 0) {
		fp = holdfp(td, uap->fd, -1);
		if (fp == NULL) {
			error = EBADF;
			goto done;
		}
	} else {
		fp = NULL;
	}

	/*
	 * Execute the internal kernel function and clean up.
	 */
	error = kern_mountctl(path, uap->op, fp, ctl, uap->ctllen,
			      buf, uap->buflen, &sysmsg->sysmsg_result);
	if (fp)
		dropfp(td, uap->fd, fp);
	if (error == 0 && sysmsg->sysmsg_result > 0)
		error = copyout(buf, uap->buf, sysmsg->sysmsg_result);
done:
	if (path)
		objcache_put(namei_oc, path);
	if (ctl)
		kfree(ctl, M_TEMP);
	if (buf)
		kfree(buf, M_TEMP);
	return (error);
}

/*
 * Execute a mount control operation by resolving the path to a mount point
 * and calling vop_mountctl().  
 *
 * Use the mount point from the nch instead of the vnode so nullfs mounts
 * can properly spike the VOP.
 */
int
kern_mountctl(const char *path, int op, struct file *fp, 
		const void *ctl, int ctllen, 
		void *buf, int buflen, int *res)
{
	struct vnode *vp;
	struct nlookupdata nd;
	struct nchandle nch;
	struct mount *mp;
	int error;

	*res = 0;
	vp = NULL;
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error)
		return (error);
	error = nlookup(&nd);
	if (error) {
		nlookup_done(&nd);
		return (error);
	}
	error = cache_vget(&nd.nl_nch, nd.nl_cred, LK_EXCLUSIVE, &vp);
	if (error) {
		nlookup_done(&nd);
		return (error);
	}

	/*
	 * Yes, all this is needed to use the nch.mount below, because
	 * we must maintain a ref on the mount to avoid ripouts (e.g.
	 * due to heavy mount/unmount use by synth or poudriere).
	 */
	nch = nd.nl_nch;
	cache_zero(&nd.nl_nch);
	cache_unlock(&nch);
	nlookup_done(&nd);
	vn_unlock(vp);

	mp = nch.mount;

	/*
	 * Must be the root of the filesystem
	 */
	if ((vp->v_flag & (VROOT|VPFSROOT)) == 0) {
		cache_drop(&nch);
		vrele(vp);
		return (EINVAL);
	}
	if (mp == NULL || mp->mnt_kern_flag & MNTK_UNMOUNT) {
		kprintf("kern_mountctl: Warning, \"%s\" racing unmount\n",
			path);
		cache_drop(&nch);
		vrele(vp);
		return (EINVAL);
	}
	error = vop_mountctl(mp->mnt_vn_use_ops, vp, op, fp, ctl, ctllen,
			     buf, buflen, res);
	vrele(vp);
	cache_drop(&nch);

	return (error);
}

int
kern_statfs(struct nlookupdata *nd, struct statfs *buf)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct mount *mp;
	struct statfs *sp;
	char *fullpath, *freepath;
	int error;

	if ((error = nlookup(nd)) != 0)
		return (error);
	mp = nd->nl_nch.mount;
	sp = &mp->mnt_stat;

	/*
	 * Ignore refresh error, user should have visibility.
	 * This can happen if a NFS mount goes bad (e.g. server
	 * revokes perms or goes down).
	 */
	error = VFS_STATFS(mp, sp, nd->nl_cred);
	/* ignore error */

	error = mount_path(p, mp, &fullpath, &freepath);
	if (error)
		return(error);
	bzero(sp->f_mntonname, sizeof(sp->f_mntonname));
	strlcpy(sp->f_mntonname, fullpath, sizeof(sp->f_mntonname));
	kfree(freepath, M_TEMP);

	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	bcopy(sp, buf, sizeof(*buf));
	/* Only root should have access to the fsid's. */
	if (priv_check(td, PRIV_ROOT))
		buf->f_fsid.val[0] = buf->f_fsid.val[1] = 0;
	return (0);
}

/*
 * statfs_args(char *path, struct statfs *buf)
 *
 * Get filesystem statistics.
 */
int
sys_statfs(struct sysmsg *sysmsg, const struct statfs_args *uap)
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
	char *fullpath, *freepath;
	int error;

	KKASSERT(p);
	if ((error = holdvnode(td, fd, &fp)) != 0)
		return (error);

	/*
	 * Try to use mount info from any overlays rather than the
	 * mount info for the underlying vnode, otherwise we will
	 * fail when operating on null-mounted paths inside a chroot.
	 */
	if ((mp = fp->f_nchandle.mount) == NULL)
		mp = ((struct vnode *)fp->f_data)->v_mount;
	if (mp == NULL) {
		error = EBADF;
		goto done;
	}
	if (fp->f_cred == NULL) {
		error = EINVAL;
		goto done;
	}

	/*
	 * Ignore refresh error, user should have visibility.
	 * This can happen if a NFS mount goes bad (e.g. server
	 * revokes perms or goes down).
	 */
	sp = &mp->mnt_stat;
	error = VFS_STATFS(mp, sp, fp->f_cred);

	if ((error = mount_path(p, mp, &fullpath, &freepath)) != 0)
		goto done;
	bzero(sp->f_mntonname, sizeof(sp->f_mntonname));
	strlcpy(sp->f_mntonname, fullpath, sizeof(sp->f_mntonname));
	kfree(freepath, M_TEMP);

	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	bcopy(sp, buf, sizeof(*buf));

	/* Only root should have access to the fsid's. */
	if (priv_check(td, PRIV_ROOT))
		buf->f_fsid.val[0] = buf->f_fsid.val[1] = 0;
	error = 0;
done:
	fdrop(fp);
	return (error);
}

/*
 * fstatfs_args(int fd, struct statfs *buf)
 *
 * Get filesystem statistics.
 */
int
sys_fstatfs(struct sysmsg *sysmsg, const struct fstatfs_args *uap)
{
	struct statfs buf;
	int error;

	error = kern_fstatfs(uap->fd, &buf);

	if (error == 0)
		error = copyout(&buf, uap->buf, sizeof(*uap->buf));
	return (error);
}

int
kern_statvfs(struct nlookupdata *nd, struct statvfs *buf)
{
	struct mount *mp;
	struct statvfs *sp;
	int error;

	if ((error = nlookup(nd)) != 0)
		return (error);
	mp = nd->nl_nch.mount;
	sp = &mp->mnt_vstat;
	if ((error = VFS_STATVFS(mp, sp, nd->nl_cred)) != 0)
		return (error);

	sp->f_flag = 0;
	if (mp->mnt_flag & MNT_RDONLY)
		sp->f_flag |= ST_RDONLY;
	if (mp->mnt_flag & MNT_NOSUID)
		sp->f_flag |= ST_NOSUID;
	bcopy(sp, buf, sizeof(*buf));
	return (0);
}

/*
 * statfs_args(char *path, struct statfs *buf)
 *
 * Get filesystem statistics.
 */
int
sys_statvfs(struct sysmsg *sysmsg, const struct statvfs_args *uap)
{
	struct nlookupdata nd;
	struct statvfs buf;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_statvfs(&nd, &buf);
	nlookup_done(&nd);
	if (error == 0)
		error = copyout(&buf, uap->buf, sizeof(*uap->buf));
	return (error);
}

int
kern_fstatvfs(int fd, struct statvfs *buf)
{
	struct thread *td = curthread;
	struct file *fp;
	struct mount *mp;
	struct statvfs *sp;
	int error;

	if ((error = holdvnode(td, fd, &fp)) != 0)
		return (error);
	if ((mp = fp->f_nchandle.mount) == NULL)
		mp = ((struct vnode *)fp->f_data)->v_mount;
	if (mp == NULL) {
		error = EBADF;
		goto done;
	}
	if (fp->f_cred == NULL) {
		error = EINVAL;
		goto done;
	}
	sp = &mp->mnt_vstat;
	if ((error = VFS_STATVFS(mp, sp, fp->f_cred)) != 0)
		goto done;

	sp->f_flag = 0;
	if (mp->mnt_flag & MNT_RDONLY)
		sp->f_flag |= ST_RDONLY;
	if (mp->mnt_flag & MNT_NOSUID)
		sp->f_flag |= ST_NOSUID;

	bcopy(sp, buf, sizeof(*buf));
	error = 0;
done:
	fdrop(fp);
	return (error);
}

/*
 * fstatfs_args(int fd, struct statfs *buf)
 *
 * Get filesystem statistics.
 */
int
sys_fstatvfs(struct sysmsg *sysmsg, const struct fstatvfs_args *uap)
{
	struct statvfs buf;
	int error;

	error = kern_fstatvfs(uap->fd, &buf);

	if (error == 0)
		error = copyout(&buf, uap->buf, sizeof(*uap->buf));
	return (error);
}

/*
 * getfsstat_args(struct statfs *buf, long bufsize, int flags)
 *
 * Get statistics on all filesystems.
 */

struct getfsstat_info {
	struct statfs *sfsp;
	long count;
	long maxcount;
	int error;
	int flags;
	struct thread *td;
};

static int getfsstat_callback(struct mount *, void *);

int
sys_getfsstat(struct sysmsg *sysmsg, const struct getfsstat_args *uap)
{
	struct thread *td = curthread;
	struct getfsstat_info info;

	bzero(&info, sizeof(info));

	info.maxcount = uap->bufsize / sizeof(struct statfs);
	info.sfsp = uap->buf;
	info.count = 0;
	info.flags = uap->flags;
	info.td = td;

	mountlist_scan(getfsstat_callback, &info, MNTSCAN_FORWARD);
	if (info.sfsp && info.count > info.maxcount)
		sysmsg->sysmsg_result = info.maxcount;
	else
		sysmsg->sysmsg_result = info.count;
	return (info.error);
}

static int
getfsstat_callback(struct mount *mp, void *data)
{
	struct getfsstat_info *info = data;
	struct statfs *sp;
	char *freepath;
	char *fullpath;
	int error;

	if (info->td->td_proc && !chroot_visible_mnt(mp, info->td->td_proc))
		return(0);

	if (info->sfsp && info->count < info->maxcount) {
		sp = &mp->mnt_stat;

		/*
		 * If MNT_NOWAIT or MNT_LAZY is specified, do not
		 * refresh the fsstat cache. MNT_NOWAIT or MNT_LAZY
		 * overrides MNT_WAIT.
		 *
		 * Ignore refresh error, user should have visibility.
		 * This can happen if a NFS mount goes bad (e.g. server
		 * revokes perms or goes down).
		 */
		if (((info->flags & (MNT_LAZY|MNT_NOWAIT)) == 0 ||
		    (info->flags & MNT_WAIT)) &&
		    (error = VFS_STATFS(mp, sp, info->td->td_ucred))) {
			/* ignore error */
		}
		sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;

		error = mount_path(info->td->td_proc, mp, &fullpath, &freepath);
		if (error) {
			info->error = error;
			return(-1);
		}
		bzero(sp->f_mntonname, sizeof(sp->f_mntonname));
		strlcpy(sp->f_mntonname, fullpath, sizeof(sp->f_mntonname));
		kfree(freepath, M_TEMP);

		error = copyout(sp, info->sfsp, sizeof(*sp));
		if (error) {
			info->error = error;
			return (-1);
		}
		++info->sfsp;
	}
	info->count++;
	return(0);
}

/*
 * getvfsstat_args(struct statfs *buf, struct statvfs *vbuf,
		   long bufsize, int flags)
 *
 * Get statistics on all filesystems.
 */

struct getvfsstat_info {
	struct statfs *sfsp;
	struct statvfs *vsfsp;
	long count;
	long maxcount;
	int error;
	int flags;
	struct thread *td;
};

static int getvfsstat_callback(struct mount *, void *);

int
sys_getvfsstat(struct sysmsg *sysmsg, const struct getvfsstat_args *uap)
{
	struct thread *td = curthread;
	struct getvfsstat_info info;

	bzero(&info, sizeof(info));

	info.maxcount = uap->vbufsize / sizeof(struct statvfs);
	info.sfsp = uap->buf;
	info.vsfsp = uap->vbuf;
	info.count = 0;
	info.flags = uap->flags;
	info.td = td;

	mountlist_scan(getvfsstat_callback, &info, MNTSCAN_FORWARD);
	if (info.vsfsp && info.count > info.maxcount)
		sysmsg->sysmsg_result = info.maxcount;
	else
		sysmsg->sysmsg_result = info.count;
	return (info.error);
}

static int
getvfsstat_callback(struct mount *mp, void *data)
{
	struct getvfsstat_info *info = data;
	struct statfs *sp;
	struct statvfs *vsp;
	char *freepath;
	char *fullpath;
	int error;

	if (info->td->td_proc && !chroot_visible_mnt(mp, info->td->td_proc))
		return(0);

	if (info->vsfsp && info->count < info->maxcount) {
		sp = &mp->mnt_stat;
		vsp = &mp->mnt_vstat;

		/*
		 * If MNT_NOWAIT or MNT_LAZY is specified, do not
		 * refresh the fsstat cache. MNT_NOWAIT or MNT_LAZY
		 * overrides MNT_WAIT.
		 *
		 * Ignore refresh error, user should have visibility.
		 * This can happen if a NFS mount goes bad (e.g. server
		 * revokes perms or goes down).
		 */
		if (((info->flags & (MNT_LAZY|MNT_NOWAIT)) == 0 ||
		    (info->flags & MNT_WAIT)) &&
		    (error = VFS_STATFS(mp, sp, info->td->td_ucred))) {
			/* ignore error */
		}
		sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;

		if (((info->flags & (MNT_LAZY|MNT_NOWAIT)) == 0 ||
		    (info->flags & MNT_WAIT)) &&
		    (error = VFS_STATVFS(mp, vsp, info->td->td_ucred))) {
			/* ignore error */
		}
		vsp->f_flag = 0;
		if (mp->mnt_flag & MNT_RDONLY)
			vsp->f_flag |= ST_RDONLY;
		if (mp->mnt_flag & MNT_NOSUID)
			vsp->f_flag |= ST_NOSUID;

		error = mount_path(info->td->td_proc, mp, &fullpath, &freepath);
		if (error) {
			info->error = error;
			return(-1);
		}
		bzero(sp->f_mntonname, sizeof(sp->f_mntonname));
		strlcpy(sp->f_mntonname, fullpath, sizeof(sp->f_mntonname));
		kfree(freepath, M_TEMP);

		error = copyout(sp, info->sfsp, sizeof(*sp));
		if (error == 0)
			error = copyout(vsp, info->vsfsp, sizeof(*vsp));
		if (error) {
			info->error = error;
			return (-1);
		}
		++info->sfsp;
		++info->vsfsp;
	}
	info->count++;
	return(0);
}


/*
 * fchdir_args(int fd)
 *
 * Change current working directory to a given file descriptor.
 */
int
sys_fchdir(struct sysmsg *sysmsg, const struct fchdir_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct vnode *vp, *ovp;
	struct mount *mp;
	struct file *fp;
	struct nchandle nch, onch, tnch;
	int error;

	if ((error = holdvnode(td, uap->fd, &fp)) != 0)
		return (error);
	lwkt_gettoken(&p->p_token);
	vp = (struct vnode *)fp->f_data;
	vref(vp);
	vn_lock(vp, LK_SHARED | LK_RETRY);
	if (fp->f_nchandle.ncp == NULL)
		error = ENOTDIR;
	else
		error = checkvp_chdir(vp, td);
	if (error) {
		vput(vp);
		goto done;
	}
	cache_copy(&fp->f_nchandle, &nch);

	/*
	 * If the ncp has become a mount point, traverse through
	 * the mount point.
	 */

	while (!error && (nch.ncp->nc_flag & NCF_ISMOUNTPT) &&
	       (mp = cache_findmount(&nch)) != NULL
	) {
		error = nlookup_mp(mp, &tnch);
		if (error == 0) {
			cache_unlock(&tnch);	/* leave ref intact */
			vput(vp);
			vp = tnch.ncp->nc_vp;
			error = vget(vp, LK_SHARED);
			KKASSERT(error == 0);
			cache_drop(&nch);
			nch = tnch;
		}
		cache_dropmount(mp);
	}
	if (error == 0) {
		spin_lock(&fdp->fd_spin);
		ovp = fdp->fd_cdir;
		onch = fdp->fd_ncdir;
		fdp->fd_cdir = vp;
		fdp->fd_ncdir = nch;
		spin_unlock(&fdp->fd_spin);
		vn_unlock(vp);		/* leave ref intact */
		cache_drop(&onch);
		vrele(ovp);
	} else {
		cache_drop(&nch);
		vput(vp);
	}
	fdrop(fp);
done:
	lwkt_reltoken(&p->p_token);
	return (error);
}

int
kern_chdir(struct nlookupdata *nd)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct vnode *vp, *ovp;
	struct nchandle onch;
	int error;

	nd->nl_flags |= NLC_SHAREDLOCK;
	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((vp = nd->nl_nch.ncp->nc_vp) == NULL)
		return (ENOENT);
	if ((error = vget(vp, LK_SHARED)) != 0)
		return (error);

	lwkt_gettoken(&p->p_token);
	error = checkvp_chdir(vp, td);
	vn_unlock(vp);
	if (error == 0) {
		spin_lock(&fdp->fd_spin);
		ovp = fdp->fd_cdir;
		onch = fdp->fd_ncdir;
		fdp->fd_ncdir = nd->nl_nch;
		fdp->fd_cdir = vp;
		spin_unlock(&fdp->fd_spin);
		cache_unlock(&nd->nl_nch);	/* leave reference intact */
		cache_drop(&onch);
		vrele(ovp);
		cache_zero(&nd->nl_nch);
	} else {
		vrele(vp);
	}
	lwkt_reltoken(&p->p_token);
	return (error);
}

/*
 * chdir_args(char *path)
 *
 * Change current working directory (``.'').
 */
int
sys_chdir(struct sysmsg *sysmsg, const struct chdir_args *uap)
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
chroot_refuse_vdir_fds(thread_t td, struct filedesc *fdp)
{
	struct vnode *vp;
	struct file *fp;
	int error;
	int fd;

	for (fd = 0; fd < fdp->fd_nfiles ; fd++) {
		if ((error = holdvnode(td, fd, &fp)) != 0)
			continue;
		vp = (struct vnode *)fp->f_data;
		if (vp->v_type != VDIR) {
			fdrop(fp);
			continue;
		}
		fdrop(fp);
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
int
kern_chroot(struct nchandle *nch)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct vnode *vp;
	int error;

	/*
	 * Only privileged user can chroot
	 */
	error = priv_check_cred(td->td_ucred, PRIV_VFS_CHROOT, 0);
	if (error)
		return (error);

	/*
	 * Disallow open directory descriptors (fchdir() breakouts).
	 */
	if (chroot_allow_open_directories == 0 ||
	   (chroot_allow_open_directories == 1 && fdp->fd_rdir != rootvnode)) {
		if ((error = chroot_refuse_vdir_fds(td, fdp)) != 0)
			return (error);
	}
	if ((vp = nch->ncp->nc_vp) == NULL)
		return (ENOENT);

	if ((error = vget(vp, LK_SHARED)) != 0)
		return (error);

	/*
	 * Check the validity of vp as a directory to change to and 
	 * associate it with rdir/jdir.
	 */
	error = checkvp_chdir(vp, td);
	vn_unlock(vp);			/* leave reference intact */
	if (error == 0) {
		lwkt_gettoken(&p->p_token);
		vrele(fdp->fd_rdir);
		fdp->fd_rdir = vp;	/* reference inherited by fd_rdir */
		cache_drop(&fdp->fd_nrdir);
		cache_copy(nch, &fdp->fd_nrdir);
		if (fdp->fd_jdir == NULL) {
			fdp->fd_jdir = vp;
			vref(fdp->fd_jdir);
			cache_copy(nch, &fdp->fd_njdir);
		}
		if ((p->p_flags & P_DIDCHROOT) == 0) {
			p->p_flags |= P_DIDCHROOT;
			if (p->p_depth <= 65535 - 32)
				p->p_depth += 32;
		}
		lwkt_reltoken(&p->p_token);
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
int
sys_chroot(struct sysmsg *sysmsg, const struct chroot_args *uap)
{
	struct thread *td __debugvar = curthread;
	struct nlookupdata nd;
	int error;

	KKASSERT(td->td_proc);
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0) {
		nd.nl_flags |= NLC_EXEC;
		error = nlookup(&nd);
		if (error == 0)
			error = kern_chroot(&nd.nl_nch);
	}
	nlookup_done(&nd);
	return(error);
}

int
sys_chroot_kernel(struct sysmsg *sysmsg, const struct chroot_kernel_args *uap)
{
	struct thread *td = curthread;
	struct nlookupdata nd;
	struct nchandle *nch;
	struct vnode *vp;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error)
		goto error_nond;

	error = nlookup(&nd);
	if (error)
		goto error_out;

	nch = &nd.nl_nch;

	error = priv_check_cred(td->td_ucred, PRIV_VFS_CHROOT, 0);
	if (error)
		goto error_out;

	if ((vp = nch->ncp->nc_vp) == NULL) {
		error = ENOENT;
		goto error_out;
	}

	if ((error = cache_vref(nch, nd.nl_cred, &vp)) != 0)
		goto error_out;

	vfs_cache_setroot(vp, cache_hold(nch));

error_out:
	nlookup_done(&nd);
error_nond:
	return(error);
}

/*
 * Common routine for chroot and chdir.  Given a locked, referenced vnode,
 * determine whether it is legal to chdir to the vnode.  The vnode's state
 * is not changed by this call.
 */
static int
checkvp_chdir(struct vnode *vp, struct thread *td)
{
	int error;

	if (vp->v_type != VDIR)
		error = ENOTDIR;
	else
		error = VOP_EACCESS(vp, VEXEC, td->td_ucred);
	return (error);
}

int
kern_open(struct nlookupdata *nd, int oflags, int mode, int *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct lwp *lp = td->td_lwp;
	struct filedesc *fdp = p->p_fd;
	int cmode, flags;
	struct file *nfp;
	struct file *fp;
	int type, indx, error = 0;
	struct flock lf;

	if ((oflags & O_ACCMODE) == O_ACCMODE)
		return (EINVAL);
	flags = FFLAGS(oflags);
	error = falloc(lp, &nfp, NULL);
	if (error)
		return (error);
	fp = nfp;
	cmode = ((mode &~ fdp->fd_cmask) & ALLPERMS) & ~S_ISTXT;

	/*
	 * Call vn_open() to do the lookup and assign the vnode to the 
	 * file pointer.  vn_open() does not change the ref count on fp
	 * and the vnode, on success, will be inherited by the file pointer
	 * and unlocked.
	 *
	 * Request a shared lock on the vnode if possible.
	 *
	 * When NLC_SHAREDLOCK is set we may still need an exclusive vnode
	 * lock for O_RDWR opens on executables in order to avoid a VTEXT
	 * detection race.  The NLC_EXCLLOCK_IFEXEC handles this case.
	 *
	 * NOTE: We need a flag to separate terminal vnode locking from
	 *	 parent locking.  O_CREAT needs parent locking, but O_TRUNC
	 *	 and O_RDWR only need to lock the terminal vnode exclusively.
	 */
	nd->nl_flags |= NLC_LOCKVP;
	if ((flags & (O_CREAT|O_TRUNC)) == 0) {
		nd->nl_flags |= NLC_SHAREDLOCK;
		if (flags & O_RDWR)
			nd->nl_flags |= NLC_EXCLLOCK_IFEXEC;
	}

	/*
	 * Issue the vn_open, passing in the referenced fp.  the vn_open()
	 * is allowed to replace fp by fdrop()ing it and returning its own
	 * referenced fp.
	 */
	nfp = fp;
	error = vn_open(nd, &nfp, flags, cmode);
	fp = nfp;
	nlookup_done(nd);

	/*
	 * Deal with any error condition
	 */
	if (error) {
		fdrop(fp);	/* our ref */
		if (error == ERESTART)
			error = EINTR;
		return (error);
	}

	/*
	 * Reserve a file descriptor.
	 */
	if ((error = fdalloc(p, 0, &indx)) != 0) {
		fdrop(fp);
		return (error);
	}

	/*
	 * Handle advisory lock flags.  This is only supported with vnodes.
	 * For things like /dev/fd/N we might not actually get a vnode.
	 */
	if ((flags & (O_EXLOCK | O_SHLOCK)) && fp->f_type == DTYPE_VNODE) {
		struct vnode *vp;

		vp = (struct vnode *)fp->f_data;
		vref(vp);

		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		if (flags & O_EXLOCK)
			lf.l_type = F_WRLCK;
		else
			lf.l_type = F_RDLCK;
		if (flags & FNONBLOCK)
			type = 0;
		else
			type = F_WAIT;

		error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, type);
		if (error) {
			/*
			 * lock request failed.  Clean up the reserved
			 * descriptor.
			 */
			vrele(vp);
			fsetfd(fdp, NULL, indx);
			fdrop(fp);
			return (error);
		}
		atomic_set_int(&fp->f_flag, FHASLOCK); /* race ok */
		vrele(vp);
	}

	/*
	 * release our private reference, leaving the one associated with the
	 * descriptor table intact.
	 */
	if (oflags & O_CLOEXEC)
		fdp->fd_files[indx].fileflags |= UF_EXCLOSE;
	fsetfd(fdp, fp, indx);
	fdrop(fp);
	*res = indx;

	return (error);
}

/*
 * open_args(char *path, int flags, int mode)
 *
 * Check permissions, allocate an open file structure,
 * and call the device open routine if any.
 */
int
sys_open(struct sysmsg *sysmsg, const struct open_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		error = kern_open(&nd, uap->flags,
				    uap->mode, &sysmsg->sysmsg_result);
	}
	nlookup_done(&nd);
	return (error);
}

/*
 * openat_args(int fd, char *path, int flags, int mode)
 */
int
sys_openat(struct sysmsg *sysmsg, const struct openat_args *uap)
{
	struct nlookupdata nd;
	int error;
	struct file *fp;

	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		error = kern_open(&nd, uap->flags, uap->mode, 
					&sysmsg->sysmsg_result);
	}
	nlookup_done_at(&nd, fp);
	return (error);
}

int
kern_mknod(struct nlookupdata *nd, int mode, int rmajor, int rminor)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	int whiteout = 0;

	KKASSERT(p);

	VATTR_NULL(&vattr);
	vattr.va_mode = (mode & ALLPERMS) &~ p->p_fd->fd_cmask;
	vattr.va_rmajor = rmajor;
	vattr.va_rminor = rminor;

	switch (mode & S_IFMT) {
	case S_IFMT:	/* used by badsect to flag bad sectors */
		error = priv_check_cred(td->td_ucred, PRIV_VFS_MKNOD_BAD, 0);
		vattr.va_type = VBAD;
		break;
	case S_IFCHR:
		error = priv_check(td, PRIV_VFS_MKNOD_DEV);
		vattr.va_type = VCHR;
		break;
	case S_IFBLK:
		error = priv_check(td, PRIV_VFS_MKNOD_DEV);
		vattr.va_type = VBLK;
		break;
	case S_IFWHT:
		error = priv_check_cred(td->td_ucred, PRIV_VFS_MKNOD_WHT, 0);
		whiteout = 1;
		break;
	case S_IFDIR:	/* special directories support for HAMMER */
		error = priv_check_cred(td->td_ucred, PRIV_VFS_MKNOD_DIR, 0);
		vattr.va_type = VDIR;
		break;
	case S_IFIFO:
		return (kern_mkfifo(nd, mode));
		break;
	default:
		error = EINVAL;
		break;
	}

	if (error)
		return (error);

	bwillinode(1);
	nd->nl_flags |= NLC_CREATE | NLC_REFDVP;
	if ((error = nlookup(nd)) != 0)
		return (error);
	if (nd->nl_nch.ncp->nc_vp)
		return (EEXIST);
	if (nd->nl_dvp == NULL)
		return (EINVAL);
	if ((error = ncp_writechk(&nd->nl_nch)) != 0)
		return (error);

	if (whiteout) {
		error = VOP_NWHITEOUT(&nd->nl_nch, nd->nl_dvp,
				      nd->nl_cred, NAMEI_CREATE);
	} else {
		vp = NULL;
		error = VOP_NMKNOD(&nd->nl_nch, nd->nl_dvp,
				   &vp, nd->nl_cred, &vattr);
		if (error == 0)
			vput(vp);
	}
	return (error);
}

/*
 * mknod_args(char *path, int mode, int dev)
 *
 * Create a special file.
 */
int
sys_mknod(struct sysmsg *sysmsg, const struct mknod_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		error = kern_mknod(&nd, uap->mode,
				   umajor(uap->dev), uminor(uap->dev));
	}
	nlookup_done(&nd);
	return (error);
}

/*
 * mknodat_args(int fd, char *path, mode_t mode, dev_t dev)
 *
 * Create a special file.  The path is relative to the directory associated
 * with fd.
 */
int
sys_mknodat(struct sysmsg *sysmsg, const struct mknodat_args *uap)
{
	struct nlookupdata nd;
	struct file *fp;
	int error;

	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		error = kern_mknod(&nd, uap->mode,
				   umajor(uap->dev), uminor(uap->dev));
	}
	nlookup_done_at(&nd, fp);
	return (error);
}

int
kern_mkfifo(struct nlookupdata *nd, int mode)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vattr vattr;
	struct vnode *vp;
	int error;

	bwillinode(1);

	nd->nl_flags |= NLC_CREATE | NLC_REFDVP;
	if ((error = nlookup(nd)) != 0)
		return (error);
	if (nd->nl_nch.ncp->nc_vp)
		return (EEXIST);
	if (nd->nl_dvp == NULL)
		return (EINVAL);
	if ((error = ncp_writechk(&nd->nl_nch)) != 0)
		return (error);

	VATTR_NULL(&vattr);
	vattr.va_type = VFIFO;
	vattr.va_mode = (mode & ALLPERMS) &~ p->p_fd->fd_cmask;
	vp = NULL;
	error = VOP_NMKNOD(&nd->nl_nch, nd->nl_dvp, &vp, nd->nl_cred, &vattr);
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
sys_mkfifo(struct sysmsg *sysmsg, const struct mkfifo_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_mkfifo(&nd, uap->mode);
	nlookup_done(&nd);
	return (error);
}

/*
 * mkfifoat_args(int fd, char *path, mode_t mode)
 *
 * Create a named pipe.  The path is relative to the directory associated
 * with fd.
 */
int
sys_mkfifoat(struct sysmsg *sysmsg, const struct mkfifoat_args *uap)
{
	struct nlookupdata nd;
	struct file *fp;
	int error;

	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_mkfifo(&nd, uap->mode);
	nlookup_done_at(&nd, fp);
	return (error);
}

static int hardlink_check_uid = 0;
SYSCTL_INT(_security, OID_AUTO, hardlink_check_uid, CTLFLAG_RW,
    &hardlink_check_uid, 0, 
    "Unprivileged processes cannot create hard links to files owned by other "
    "users");
static int hardlink_check_gid = 0;
SYSCTL_INT(_security, OID_AUTO, hardlink_check_gid, CTLFLAG_RW,
    &hardlink_check_gid, 0,
    "Unprivileged processes cannot create hard links to files owned by other "
    "groups");

static int
can_hardlink(struct vnode *vp, struct thread *td, struct ucred *cred)
{
	struct vattr va;
	int error;

	/*
	 * Shortcut if disabled
	 */
	if (hardlink_check_uid == 0 && hardlink_check_gid == 0)
		return (0);

	/*
	 * Privileged user can always hardlink
	 */
	if (priv_check_cred(cred, PRIV_VFS_LINK, 0) == 0)
		return (0);

	/*
	 * Otherwise only if the originating file is owned by the
	 * same user or group.  Note that any group is allowed if
	 * the file is owned by the caller.
	 */
	error = VOP_GETATTR(vp, &va);
	if (error != 0)
		return (error);
	
	if (hardlink_check_uid) {
		if (cred->cr_uid != va.va_uid)
			return (EPERM);
	}
	
	if (hardlink_check_gid) {
		if (cred->cr_uid != va.va_uid && !groupmember(va.va_gid, cred))
			return (EPERM);
	}

	return (0);
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
	 * You may only hardlink a file which you have write permission
	 * on or which you own.
	 *
	 * XXX relookup on vget failure / race ?
	 */
	bwillinode(1);
	nd->nl_flags |= NLC_WRITE | NLC_OWN | NLC_HLINK;
	if ((error = nlookup(nd)) != 0)
		return (error);
	vp = nd->nl_nch.ncp->nc_vp;
	KKASSERT(vp != NULL);
	if (vp->v_type == VDIR)
		return (EPERM);		/* POSIX */
	if ((error = ncp_writechk(&nd->nl_nch)) != 0)
		return (error);
	if ((error = vget(vp, LK_EXCLUSIVE)) != 0)
		return (error);

	/*
	 * Unlock the source so we can lookup the target without deadlocking
	 * (XXX vp is locked already, possible other deadlock?).  The target
	 * must not exist.
	 */
	KKASSERT(nd->nl_flags & NLC_NCPISLOCKED);
	nd->nl_flags &= ~NLC_NCPISLOCKED;
	cache_unlock(&nd->nl_nch);
	vn_unlock(vp);

	linknd->nl_flags |= NLC_CREATE | NLC_REFDVP;
	if ((error = nlookup(linknd)) != 0) {
		vrele(vp);
		return (error);
	}
	if (linknd->nl_nch.ncp->nc_vp) {
		vrele(vp);
		return (EEXIST);
	}
	if (linknd->nl_dvp == NULL) {
		vrele(vp);
		return (EINVAL);
	}
	VFS_MODIFYING(vp->v_mount);
	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY | LK_FAILRECLAIM);
	if (error) {
		vrele(vp);
		return (error);
	}

	/*
	 * Finally run the new API VOP.
	 */
	error = can_hardlink(vp, td, td->td_ucred);
	if (error == 0) {
		error = VOP_NLINK(&linknd->nl_nch, linknd->nl_dvp,
				  vp, linknd->nl_cred);
	}
	vput(vp);
	return (error);
}

/*
 * link_args(char *path, char *link)
 *
 * Make a hard file link.
 */
int
sys_link(struct sysmsg *sysmsg, const struct link_args *uap)
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

/*
 * linkat_args(int fd1, char *path1, int fd2, char *path2, int flags)
 *
 * Make a hard file link. The path1 argument is relative to the directory
 * associated with fd1, and similarly the path2 argument is relative to
 * the directory associated with fd2.
 */
int
sys_linkat(struct sysmsg *sysmsg, const struct linkat_args *uap)
{
	struct nlookupdata nd, linknd;
	struct file *fp1, *fp2;
	int error;

	error = nlookup_init_at(&nd, &fp1, uap->fd1, uap->path1, UIO_USERSPACE,
	    (uap->flags & AT_SYMLINK_FOLLOW) ? NLC_FOLLOW : 0);
	if (error == 0) {
		error = nlookup_init_at(&linknd, &fp2, uap->fd2,
		    uap->path2, UIO_USERSPACE, 0);
		if (error == 0)
			error = kern_link(&nd, &linknd);
		nlookup_done_at(&linknd, fp2);
	}
	nlookup_done_at(&nd, fp1);
	return (error);
}

int
kern_symlink(struct nlookupdata *nd, char *path, int mode)
{
	struct vattr vattr;
	struct vnode *vp;
	struct vnode *dvp;
	int error;

	bwillinode(1);
	nd->nl_flags |= NLC_CREATE | NLC_REFDVP;
	if ((error = nlookup(nd)) != 0)
		return (error);
	if (nd->nl_nch.ncp->nc_vp)
		return (EEXIST);
	if (nd->nl_dvp == NULL)
		return (EINVAL);
	if ((error = ncp_writechk(&nd->nl_nch)) != 0)
		return (error);
	dvp = nd->nl_dvp;
	VATTR_NULL(&vattr);
	vattr.va_mode = mode;
	error = VOP_NSYMLINK(&nd->nl_nch, dvp, &vp, nd->nl_cred, &vattr, path);
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
sys_symlink(struct sysmsg *sysmsg, const struct symlink_args *uap)
{
	struct thread *td = curthread;
	struct nlookupdata nd;
	char *path;
	int error;
	int mode;

	path = objcache_get(namei_oc, M_WAITOK);
	error = copyinstr(uap->path, path, MAXPATHLEN, NULL);
	if (error == 0) {
		error = nlookup_init(&nd, uap->link, UIO_USERSPACE, 0);
		if (error == 0) {
			mode = ACCESSPERMS & ~td->td_proc->p_fd->fd_cmask;
			error = kern_symlink(&nd, path, mode);
		}
		nlookup_done(&nd);
	}
	objcache_put(namei_oc, path);
	return (error);
}

/*
 * symlinkat_args(char *path1, int fd, char *path2)
 *
 * Make a symbolic link.  The path2 argument is relative to the directory
 * associated with fd.
 */
int
sys_symlinkat(struct sysmsg *sysmsg, const struct symlinkat_args *uap)
{
	struct thread *td = curthread;
	struct nlookupdata nd;
	struct file *fp;
	char *path1;
	int error;
	int mode;

	path1 = objcache_get(namei_oc, M_WAITOK);
	error = copyinstr(uap->path1, path1, MAXPATHLEN, NULL);
	if (error == 0) {
		error = nlookup_init_at(&nd, &fp, uap->fd, uap->path2,
		    UIO_USERSPACE, 0);
		if (error == 0) {
			mode = ACCESSPERMS & ~td->td_proc->p_fd->fd_cmask;
			error = kern_symlink(&nd, path1, mode);
		}
		nlookup_done_at(&nd, fp);
	}
	objcache_put(namei_oc, path1);
	return (error);
}

/*
 * undelete_args(char *path)
 *
 * Delete a whiteout from the filesystem.
 */
int
sys_undelete(struct sysmsg *sysmsg, const struct undelete_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	bwillinode(1);
	nd.nl_flags |= NLC_DELETE | NLC_REFDVP;
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0 && nd.nl_dvp == NULL)
		error = EINVAL;
	if (error == 0)
		error = ncp_writechk(&nd.nl_nch);
	if (error == 0) {
		error = VOP_NWHITEOUT(&nd.nl_nch, nd.nl_dvp, nd.nl_cred,
				      NAMEI_DELETE);
	}
	nlookup_done(&nd);
	return (error);
}

int
kern_unlink(struct nlookupdata *nd)
{
	int error;

	bwillinode(1);
	nd->nl_flags |= NLC_DELETE | NLC_REFDVP;
	if ((error = nlookup(nd)) != 0)
		return (error);
	if (nd->nl_dvp == NULL)
		return EINVAL;
	if ((error = ncp_writechk(&nd->nl_nch)) != 0)
		return (error);
	error = VOP_NREMOVE(&nd->nl_nch, nd->nl_dvp, nd->nl_cred);
	return (error);
}

/*
 * unlink_args(char *path)
 *
 * Delete a name from the filesystem.
 */
int
sys_unlink(struct sysmsg *sysmsg, const struct unlink_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_unlink(&nd);
	nlookup_done(&nd);
	return (error);
}


/*
 * unlinkat_args(int fd, char *path, int flags)
 *
 * Delete the file or directory entry pointed to by fd/path.
 */
int
sys_unlinkat(struct sysmsg *sysmsg, const struct unlinkat_args *uap)
{
	struct nlookupdata nd;
	struct file *fp;
	int error;

	if (uap->flags & ~AT_REMOVEDIR)
		return (EINVAL);

	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		if (uap->flags & AT_REMOVEDIR)
			error = kern_rmdir(&nd);
		else
			error = kern_unlink(&nd);
	}
	nlookup_done_at(&nd, fp);
	return (error);
}

int
kern_lseek(int fd, off_t offset, int whence, off_t *res)
{
	struct thread *td = curthread;
	struct file *fp;
	struct vnode *vp;
	struct vattr_lite lva;
	off_t new_offset;
	int error;

	fp = holdfp(td, fd, -1);
	if (fp == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_VNODE) {
		error = ESPIPE;
		goto done;
	}
	vp = (struct vnode *)fp->f_data;

	switch (whence) {
	case L_INCR:
		spin_lock(&fp->f_spin);
		new_offset = fp->f_offset + offset;
		error = 0;
		break;
	case L_XTND:
		error = VOP_GETATTR_LITE(vp, &lva);
		spin_lock(&fp->f_spin);
		new_offset = offset + lva.va_size;
		break;
	case L_SET:
		new_offset = offset;
		error = 0;
		spin_lock(&fp->f_spin);
		break;
	default:
		new_offset = 0;
		error = EINVAL;
		spin_lock(&fp->f_spin);
		break;
	}

	/*
	 * Validate the seek position.  Negative offsets are not allowed
	 * for regular files or directories.
	 *
	 * Normally we would also not want to allow negative offsets for
	 * character and block-special devices.  However kvm addresses
	 * on 64 bit architectures might appear to be negative and must
	 * be allowed.
	 */
	if (error == 0) {
		if (new_offset < 0 &&
		    (vp->v_type == VREG || vp->v_type == VDIR)) {
			error = EINVAL;
		} else {
			fp->f_offset = new_offset;
		}
	}
	*res = fp->f_offset;
	spin_unlock(&fp->f_spin);
done:
	dropfp(td, fd, fp);

	return (error);
}

/*
 * lseek_args(int fd, int pad, off_t offset, int whence)
 *
 * Reposition read/write file offset.
 */
int
sys_lseek(struct sysmsg *sysmsg, const struct lseek_args *uap)
{
	int error;

	error = kern_lseek(uap->fd, uap->offset, uap->whence,
			   &sysmsg->sysmsg_offset);

	return (error);
}

/*
 * Check if current process can access given file.  amode is a bitmask of *_OK
 * access bits.  flags is a bitmask of AT_* flags.
 */
int
kern_access(struct nlookupdata *nd, int amode, int flags)
{
	struct vnode *vp;
	int error, mode;

	if (flags & ~AT_EACCESS)
		return (EINVAL);
	nd->nl_flags |= NLC_SHAREDLOCK;
	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((amode & W_OK) && (error = ncp_writechk(&nd->nl_nch)) != 0)
		return (error);
retry:
	error = cache_vget(&nd->nl_nch, nd->nl_cred, LK_SHARED, &vp);
	if (error)
		return (error);

	/* Flags == 0 means only check for existence. */
	if (amode) {
		mode = 0;
		if (amode & R_OK)
			mode |= VREAD;
		if (amode & W_OK)
			mode |= VWRITE;
		if (amode & X_OK)
			mode |= VEXEC;
		if ((mode & VWRITE) == 0 || 
		    (error = vn_writechk(vp)) == 0) {
			error = VOP_ACCESS_FLAGS(vp, mode, flags, nd->nl_cred);
		}

		/*
		 * If the file handle is stale we have to re-resolve the
		 * entry with the ncp held exclusively.  This is a hack
		 * at the moment.
		 */
		if (error == ESTALE) {
			u_int dummy_gen;

			vput(vp);
			cache_unlock(&nd->nl_nch);
			cache_lock(&nd->nl_nch);
			dummy_gen = nd->nl_nch.ncp->nc_generation;
			cache_setunresolved(&nd->nl_nch);
			error = cache_resolve(&nd->nl_nch, &dummy_gen,
					      nd->nl_cred);
			if (error == 0) {
				vp = NULL;
				goto retry;
			}
			return(error);
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
sys_access(struct sysmsg *sysmsg, const struct access_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_access(&nd, uap->flags, 0);
	nlookup_done(&nd);
	return (error);
}


/*
 * eaccess_args(char *path, int flags)
 *
 * Check access permissions.
 */
int
sys_eaccess(struct sysmsg *sysmsg, const struct eaccess_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_access(&nd, uap->flags, AT_EACCESS);
	nlookup_done(&nd);
	return (error);
}


/*
 * faccessat_args(int fd, char *path, int amode, int flags)
 *
 * Check access permissions.
 */
int
sys_faccessat(struct sysmsg *sysmsg, const struct faccessat_args *uap)
{
	struct nlookupdata nd;
	struct file *fp;
	int error;

	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path, UIO_USERSPACE, 
				NLC_FOLLOW);
	if (error == 0)
		error = kern_access(&nd, uap->amode, uap->flags);
	nlookup_done_at(&nd, fp);
	return (error);
}

int
kern_stat(struct nlookupdata *nd, struct stat *st)
{
	int error;
	struct vnode *vp;

	nd->nl_flags |= NLC_SHAREDLOCK;
	if ((error = nlookup(nd)) != 0)
		return (error);
again:
	if ((vp = nd->nl_nch.ncp->nc_vp) == NULL)
		return (ENOENT);

#if 1
	error = cache_vref(&nd->nl_nch, NULL, &vp);
#else
	error = vget(vp, LK_SHARED);
#endif
	if (error)
		return (error);
	error = vn_stat(vp, st, nd->nl_cred);

	/*
	 * If the file handle is stale we have to re-resolve the
	 * entry with the ncp held exclusively.  This is a hack
	 * at the moment.
	 */
	if (error == ESTALE) {
		u_int dummy_gen;
#if 1
		vrele(vp);
#else
		vput(vp);
#endif
		cache_unlock(&nd->nl_nch);
		cache_lock(&nd->nl_nch);
		dummy_gen = nd->nl_nch.ncp->nc_generation;
		cache_setunresolved(&nd->nl_nch);
		error = cache_resolve(&nd->nl_nch, &dummy_gen, nd->nl_cred);
		if (error == 0)
			goto again;
	} else {
#if 1
		vrele(vp);
#else
		vput(vp);
#endif
	}
	return (error);
}

/*
 * stat_args(char *path, struct stat *ub)
 *
 * Get file status; this version follows links.
 */
int
sys_stat(struct sysmsg *sysmsg, const struct stat_args *uap)
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
sys_lstat(struct sysmsg *sysmsg, const struct lstat_args *uap)
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

/*
 * fstatat_args(int fd, char *path, struct stat *sb, int flags)
 *
 * Get status of file pointed to by fd/path.
 */
int
sys_fstatat(struct sysmsg *sysmsg, const struct fstatat_args *uap)
{
	struct nlookupdata nd;
	struct stat st;
	int error;
	int flags;
	struct file *fp;

	if (uap->flags & ~AT_SYMLINK_NOFOLLOW)
		return (EINVAL);

	flags = (uap->flags & AT_SYMLINK_NOFOLLOW) ? 0 : NLC_FOLLOW;

	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path, 
				UIO_USERSPACE, flags);
	if (error == 0) {
		error = kern_stat(&nd, &st);
		if (error == 0)
			error = copyout(&st, uap->sb, sizeof(*uap->sb));
	}
	nlookup_done_at(&nd, fp);
	return (error);
}

static int
kern_pathconf(char *path, int name, int flags, register_t *sysmsg_regp)
{
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, path, UIO_USERSPACE, flags);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(&nd.nl_nch, nd.nl_cred, LK_EXCLUSIVE, &vp);
	nlookup_done(&nd);
	if (error == 0) {
		error = VOP_PATHCONF(vp, name, sysmsg_regp);
		vput(vp);
	}
	return (error);
}

/*
 * pathconf_Args(char *path, int name)
 *
 * Get configurable pathname variables.
 */
int
sys_pathconf(struct sysmsg *sysmsg, const struct pathconf_args *uap)
{
	return (kern_pathconf(uap->path, uap->name, NLC_FOLLOW,
		&sysmsg->sysmsg_reg));
}

/*
 * lpathconf_Args(char *path, int name)
 *
 * Get configurable pathname variables, but don't follow symlinks.
 */
int
sys_lpathconf(struct sysmsg *sysmsg, const struct lpathconf_args *uap)
{
	return (kern_pathconf(uap->path, uap->name, 0, &sysmsg->sysmsg_reg));
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
	struct vnode *vp;
	struct iovec aiov;
	struct uio auio;
	int error;

	nd->nl_flags |= NLC_SHAREDLOCK;
	if ((error = nlookup(nd)) != 0)
		return (error);
	error = cache_vget(&nd->nl_nch, nd->nl_cred, LK_SHARED, &vp);
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
		error = VOP_READLINK(vp, &auio, td->td_ucred);
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
sys_readlink(struct sysmsg *sysmsg, const struct readlink_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		error = kern_readlink(&nd, uap->buf, uap->count,
					&sysmsg->sysmsg_result);
	}
	nlookup_done(&nd);
	return (error);
}

/*
 * readlinkat_args(int fd, char *path, char *buf, size_t bufsize)
 *
 * Return target name of a symbolic link.  The path is relative to the
 * directory associated with fd.
 */
int
sys_readlinkat(struct sysmsg *sysmsg, const struct readlinkat_args *uap)
{
	struct nlookupdata nd;
	struct file *fp;
	int error;

	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		error = kern_readlink(&nd, uap->buf, uap->bufsize,
					&sysmsg->sysmsg_result);
	}
	nlookup_done_at(&nd, fp);
	return (error);
}

static int
setfflags(struct vnode *vp, u_long flags)
{
	struct thread *td = curthread;
	int error;
	struct vattr vattr;

	/*
	 * Prevent non-root users from setting flags on devices.  When
	 * a device is reused, users can retain ownership of the device
	 * if they are allowed to set flags and programs assume that
	 * chown can't fail when done as root.
	 */
	if ((vp->v_type == VCHR || vp->v_type == VBLK) && 
	    ((error = priv_check_cred(td->td_ucred, PRIV_VFS_CHFLAGS_DEV, 0)) != 0))
		return (error);

	/*
	 * note: vget is required for any operation that might mod the vnode
	 * so VINACTIVE is properly cleared.
	 */
	if ((error = vget(vp, LK_EXCLUSIVE)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_flags = flags;
		error = VOP_SETATTR(vp, &vattr, td->td_ucred);
		vput(vp);
	}
	return (error);
}

/*
 * chflags(const char *path, u_long flags)
 *
 * Change flags of a file given a path name.
 */
int
sys_chflags(struct sysmsg *sysmsg, const struct chflags_args *uap)
{
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = ncp_writechk(&nd.nl_nch);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
	nlookup_done(&nd);
	if (error == 0) {
		error = setfflags(vp, uap->flags);
		vrele(vp);
	}
	return (error);
}

/*
 * lchflags(const char *path, u_long flags)
 *
 * Change flags of a file given a path name, but don't follow symlinks.
 */
int
sys_lchflags(struct sysmsg *sysmsg, const struct lchflags_args *uap)
{
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = ncp_writechk(&nd.nl_nch);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
	nlookup_done(&nd);
	if (error == 0) {
		error = setfflags(vp, uap->flags);
		vrele(vp);
	}
	return (error);
}

/*
 * fchflags_args(int fd, u_flags flags)
 *
 * Change flags of a file given a file descriptor.
 */
int
sys_fchflags(struct sysmsg *sysmsg, const struct fchflags_args *uap)
{
	struct thread *td = curthread;
	struct file *fp;
	int error;

	if ((error = holdvnode(td, uap->fd, &fp)) != 0)
		return (error);
	if (fp->f_nchandle.ncp)
		error = ncp_writechk(&fp->f_nchandle);
	if (error == 0)
		error = setfflags((struct vnode *) fp->f_data, uap->flags);
	fdrop(fp);
	return (error);
}

/*
 * chflagsat_args(int fd, const char *path, u_long flags, int atflags)
 * change flags given a pathname relative to a filedescriptor
 */
int
sys_chflagsat(struct sysmsg *sysmsg, const struct chflagsat_args *uap)
{
	struct nlookupdata nd;
	struct vnode *vp;
	struct file *fp;
	int error;
	int lookupflags;

	if (uap->atflags & ~AT_SYMLINK_NOFOLLOW)
		return (EINVAL);

	lookupflags = (uap->atflags & AT_SYMLINK_NOFOLLOW) ? 0 : NLC_FOLLOW;

	vp = NULL;
	error = nlookup_init_at(&nd, &fp, uap->fd,  uap->path, UIO_USERSPACE, lookupflags);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = ncp_writechk(&nd.nl_nch);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
	nlookup_done_at(&nd, fp);
	if (error == 0) {
		error = setfflags(vp, uap->flags);
		vrele(vp);
	}
	return (error);
}


static int
setfmode(struct vnode *vp, int mode)
{
	struct thread *td = curthread;
	int error;
	struct vattr vattr;

	/*
	 * note: vget is required for any operation that might mod the vnode
	 * so VINACTIVE is properly cleared.
	 */
	if ((error = vget(vp, LK_EXCLUSIVE)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_mode = mode & ALLPERMS;
		error = VOP_SETATTR(vp, &vattr, td->td_ucred);
		cache_inval_wxok(vp);
		vput(vp);
	}
	return error;
}

int
kern_chmod(struct nlookupdata *nd, int mode)
{
	struct vnode *vp;
	int error;

	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((error = cache_vref(&nd->nl_nch, nd->nl_cred, &vp)) != 0)
		return (error);
	if ((error = ncp_writechk(&nd->nl_nch)) == 0)
		error = setfmode(vp, mode);
	vrele(vp);
	return (error);
}

/*
 * chmod_args(char *path, int mode)
 *
 * Change mode of a file given path name.
 */
int
sys_chmod(struct sysmsg *sysmsg, const struct chmod_args *uap)
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
int
sys_lchmod(struct sysmsg *sysmsg, const struct lchmod_args *uap)
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
int
sys_fchmod(struct sysmsg *sysmsg, const struct fchmod_args *uap)
{
	struct thread *td = curthread;
	struct file *fp;
	int error;

	if ((error = holdvnode(td, uap->fd, &fp)) != 0)
		return (error);
	if (fp->f_nchandle.ncp)
		error = ncp_writechk(&fp->f_nchandle);
	if (error == 0)
		error = setfmode((struct vnode *)fp->f_data, uap->mode);
	fdrop(fp);
	return (error);
}

/*
 * fchmodat_args(char *path, int mode)
 *
 * Change mode of a file pointed to by fd/path.
 */
int
sys_fchmodat(struct sysmsg *sysmsg, const struct fchmodat_args *uap)
{
	struct nlookupdata nd;
	struct file *fp;
	int error;
	int flags;

	if (uap->flags & ~AT_SYMLINK_NOFOLLOW)
		return (EINVAL);
	flags = (uap->flags & AT_SYMLINK_NOFOLLOW) ? 0 : NLC_FOLLOW;

	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path, 
				UIO_USERSPACE, flags);
	if (error == 0)
		error = kern_chmod(&nd, uap->mode);
	nlookup_done_at(&nd, fp);
	return (error);
}

static int
setfown(struct mount *mp, struct vnode *vp, uid_t uid, gid_t gid)
{
	struct thread *td = curthread;
	int error;
	struct vattr vattr;
	uid_t o_uid;
	gid_t o_gid;
	uint64_t size;

	/*
	 * note: vget is required for any operation that might mod the vnode
	 * so VINACTIVE is properly cleared.
	 */
	if ((error = vget(vp, LK_EXCLUSIVE)) == 0) {
		if ((error = VOP_GETATTR(vp, &vattr)) != 0)
			return error;
		o_uid = vattr.va_uid;
		o_gid = vattr.va_gid;
		size = vattr.va_size;

		VATTR_NULL(&vattr);
		vattr.va_uid = uid;
		vattr.va_gid = gid;
		error = VOP_SETATTR(vp, &vattr, td->td_ucred);
		vput(vp);
	}

	if (error == 0) {
		if (uid == -1)
			uid = o_uid;
		if (gid == -1)
			gid = o_gid;
		VFS_ACCOUNT(mp, o_uid, o_gid, -size);
		VFS_ACCOUNT(mp,   uid,   gid,  size);
	}

	return error;
}

int
kern_chown(struct nlookupdata *nd, int uid, int gid)
{
	struct vnode *vp;
	int error;

	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((error = cache_vref(&nd->nl_nch, nd->nl_cred, &vp)) != 0)
		return (error);
	if ((error = ncp_writechk(&nd->nl_nch)) == 0)
		error = setfown(nd->nl_nch.mount, vp, uid, gid);
	vrele(vp);
	return (error);
}

/*
 * chown(char *path, int uid, int gid)
 *
 * Set ownership given a path name.
 */
int
sys_chown(struct sysmsg *sysmsg, const struct chown_args *uap)
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
sys_lchown(struct sysmsg *sysmsg, const struct lchown_args *uap)
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
int
sys_fchown(struct sysmsg *sysmsg, const struct fchown_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	if ((error = holdvnode(td, uap->fd, &fp)) != 0)
		return (error);
	if (fp->f_nchandle.ncp)
		error = ncp_writechk(&fp->f_nchandle);
	if (error == 0)
		error = setfown(p->p_fd->fd_ncdir.mount,
			(struct vnode *)fp->f_data, uap->uid, uap->gid);
	fdrop(fp);
	return (error);
}

/*
 * fchownat(int fd, char *path, int uid, int gid, int flags)
 *
 * Set ownership of file pointed to by fd/path.
 */
int
sys_fchownat(struct sysmsg *sysmsg, const struct fchownat_args *uap)
{
	struct nlookupdata nd;
	struct file *fp;
	int error;
	int flags;

	if (uap->flags & ~AT_SYMLINK_NOFOLLOW)
		return (EINVAL);
	flags = (uap->flags & AT_SYMLINK_NOFOLLOW) ? 0 : NLC_FOLLOW;

	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path, 
				UIO_USERSPACE, flags);
	if (error == 0)
		error = kern_chown(&nd, uap->uid, uap->gid);
	nlookup_done_at(&nd, fp);
	return (error);
}


static int
getutimes(struct timeval *tvp, struct timespec *tsp)
{
	struct timeval tv[2];
	int error;

	if (tvp == NULL) {
		microtime(&tv[0]);
		TIMEVAL_TO_TIMESPEC(&tv[0], &tsp[0]);
		tsp[1] = tsp[0];
	} else {
		if ((error = itimerfix(tvp)) != 0)
			return (error);
		TIMEVAL_TO_TIMESPEC(&tvp[0], &tsp[0]);
		TIMEVAL_TO_TIMESPEC(&tvp[1], &tsp[1]);
	}
	return 0;
}

static int
getutimens(const struct timespec *ts, struct timespec *newts, int *nullflag)
{
	struct timespec tsnow;
	int error;

	*nullflag = 0;
	nanotime(&tsnow);
	if (ts == NULL) {
		newts[0] = tsnow;
		newts[1] = tsnow;
		*nullflag = 1;
		return (0);
	}

	newts[0] = ts[0];
	newts[1] = ts[1];
	if (newts[0].tv_nsec == UTIME_OMIT && newts[1].tv_nsec == UTIME_OMIT)
		return (0);
	if (newts[0].tv_nsec == UTIME_NOW && newts[1].tv_nsec == UTIME_NOW)
		*nullflag = 1;

	if (newts[0].tv_nsec == UTIME_OMIT)
		newts[0].tv_sec = VNOVAL;
	else if (newts[0].tv_nsec == UTIME_NOW)
		newts[0] = tsnow;
	else if ((error = itimespecfix(&newts[0])) != 0)
		return (error);

	if (newts[1].tv_nsec == UTIME_OMIT)
		newts[1].tv_sec = VNOVAL;
	else if (newts[1].tv_nsec == UTIME_NOW)
		newts[1] = tsnow;
	else if ((error = itimespecfix(&newts[1])) != 0)
		return (error);

	return (0);
}

static int
setutimes(struct vnode *vp, struct vattr *vattr,
	  const struct timespec *ts, int nullflag)
{
	struct thread *td = curthread;
	int error;

	VATTR_NULL(vattr);
	vattr->va_atime = ts[0];
	vattr->va_mtime = ts[1];
	if (nullflag)
		vattr->va_vaflags |= VA_UTIMES_NULL;
	error = VOP_SETATTR(vp, vattr, td->td_ucred);

	return error;
}

int
kern_utimes(struct nlookupdata *nd, struct timeval *tptr)
{
	struct timespec ts[2];
	int error;

	if (tptr) {
		if ((error = getutimes(tptr, ts)) != 0)
			return (error);
	}
	error = kern_utimensat(nd, tptr ? ts : NULL, 0);
	return (error);
}

/*
 * utimes_args(char *path, struct timeval *tptr)
 *
 * Set the access and modification times of a file.
 */
int
sys_utimes(struct sysmsg *sysmsg, const struct utimes_args *uap)
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
sys_lutimes(struct sysmsg *sysmsg, const struct lutimes_args *uap)
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

/*
 * Set utimes on a file descriptor.  The creds used to open the
 * file are used to determine whether the operation is allowed
 * or not.
 */
int
kern_futimens(int fd, struct timespec *ts)
{
	struct thread *td = curthread;
	struct timespec newts[2];
	struct file *fp;
	struct vnode *vp;
	struct vattr vattr;
	struct vattr_lite lva;
	int nullflag;
	int error;

	error = getutimens(ts, newts, &nullflag);
	if (error)
		return (error);
	if ((error = holdvnode(td, fd, &fp)) != 0)
		return (error);
	if (fp->f_nchandle.ncp)
		error = ncp_writechk(&fp->f_nchandle);
	if (error == 0) {
		vp = fp->f_data;
		error = vget(vp, LK_EXCLUSIVE);
		if (error == 0) {
			error = VOP_GETATTR_FP(vp, &vattr, fp);
			if (error == 0) {
				lva.va_type = vattr.va_type;
				lva.va_nlink = vattr.va_nlink;
				lva.va_mode = vattr.va_mode;
				lva.va_uid = vattr.va_uid;
				lva.va_gid = vattr.va_gid;
				lva.va_size = vattr.va_size;
				lva.va_flags = vattr.va_flags;

				error = naccess_lva(&lva, NLC_OWN | NLC_WRITE,
						   fp->f_cred);
			}
			if (error == 0) {
				error = setutimes(vp, &vattr, newts, nullflag);
			}
			vput(vp);
		}
	}
	fdrop(fp);
	return (error);
}

/*
 * futimens_args(int fd, struct timespec *ts)
 *
 * Set the access and modification times of a file.
 */
int
sys_futimens(struct sysmsg *sysmsg, const struct futimens_args *uap)
{
	struct timespec ts[2];
	int error;

	if (uap->ts) {
		error = copyin(uap->ts, ts, sizeof(ts));
		if (error)
			return (error);
	}
	error = kern_futimens(uap->fd, uap->ts ? ts : NULL);
	return (error);
}

int
kern_futimes(int fd, struct timeval *tptr)
{
	struct timespec ts[2];
	int error;

	if (tptr) {
		if ((error = getutimes(tptr, ts)) != 0)
			return (error);
	}
	error = kern_futimens(fd, tptr ? ts : NULL);
	return (error);
}

/*
 * futimes_args(int fd, struct timeval *tptr)
 *
 * Set the access and modification times of a file.
 */
int
sys_futimes(struct sysmsg *sysmsg, const struct futimes_args *uap)
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
kern_utimensat(struct nlookupdata *nd, const struct timespec *ts, int flags)
{
	struct timespec newts[2];
	struct vnode *vp;
	struct vattr vattr;
	int nullflag;
	int error;

	if (flags & ~AT_SYMLINK_NOFOLLOW)
		return (EINVAL);

	error = getutimens(ts, newts, &nullflag);
	if (error)
		return (error);

	nd->nl_flags |= NLC_OWN | NLC_WRITE;
	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((error = ncp_writechk(&nd->nl_nch)) != 0)
		return (error);
	if ((error = cache_vref(&nd->nl_nch, nd->nl_cred, &vp)) != 0)
		return (error);
	if ((error = vn_writechk(vp)) == 0) {
		error = vget(vp, LK_EXCLUSIVE);
		if (error == 0) {
			error = setutimes(vp, &vattr, newts, nullflag);
			vput(vp);
		}
	}
	vrele(vp);
	return (error);
}

/*
 * utimensat_args(int fd, const char *path, const struct timespec *ts, int flags);
 *
 * Set file access and modification times of a file.
 */
int
sys_utimensat(struct sysmsg *sysmsg, const struct utimensat_args *uap)
{
	struct timespec ts[2];
	struct nlookupdata nd;
	struct file *fp;
	int error;
	int flags;

	if (uap->ts) {
		error = copyin(uap->ts, ts, sizeof(ts));
		if (error)
			return (error);
	}

	flags = (uap->flags & AT_SYMLINK_NOFOLLOW) ? 0 : NLC_FOLLOW;
	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path,
	                        UIO_USERSPACE, flags);
	if (error == 0)
		error = kern_utimensat(&nd, uap->ts ? ts : NULL, uap->flags);
	nlookup_done_at(&nd, fp);
	return (error);
}

int
kern_truncate(struct nlookupdata *nd, off_t length)
{
	struct vnode *vp;
	struct vattr vattr;
	int error;
	uid_t uid = 0;
	gid_t gid = 0;
	uint64_t old_size = 0;

	if (length < 0)
		return(EINVAL);
	nd->nl_flags |= NLC_WRITE | NLC_TRUNCATE;
	if ((error = nlookup(nd)) != 0)
		return (error);
	if ((error = ncp_writechk(&nd->nl_nch)) != 0)
		return (error);
	if ((error = cache_vref(&nd->nl_nch, nd->nl_cred, &vp)) != 0)
		return (error);
	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY | LK_FAILRECLAIM);
	if (error) {
		vrele(vp);
		return (error);
	}
	if (vp->v_type == VDIR) {
		error = EISDIR;
		goto done;
	}
	if (vfs_quota_enabled) {
		error = VOP_GETATTR(vp, &vattr);
		KASSERT(error == 0, ("kern_truncate(): VOP_GETATTR didn't return 0"));
		uid = vattr.va_uid;
		gid = vattr.va_gid;
		old_size = vattr.va_size;
	}

	if ((error = vn_writechk(vp)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_size = length;
		error = VOP_SETATTR(vp, &vattr, nd->nl_cred);
		VFS_ACCOUNT(nd->nl_nch.mount, uid, gid, length - old_size);
	}
done:
	vput(vp);
	return (error);
}

/*
 * truncate(char *path, int pad, off_t length)
 *
 * Truncate a file given its path name.
 */
int
sys_truncate(struct sysmsg *sysmsg, const struct truncate_args *uap)
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
	struct vattr vattr;
	struct vnode *vp;
	struct file *fp;
	int error;
	uid_t uid = 0;
	gid_t gid = 0;
	uint64_t old_size = 0;
	struct mount *mp;

	if (length < 0)
		return(EINVAL);
	if ((error = holdvnode(td, fd, &fp)) != 0)
		return (error);
	if (fp->f_nchandle.ncp) {
		error = ncp_writechk(&fp->f_nchandle);
		if (error)
			goto done;
	}
	if ((fp->f_flag & FWRITE) == 0) {
		error = EINVAL;
		goto done;
	}
	if (fp->f_flag & FAPPENDONLY) {	/* inode was set s/uapnd */
		error = EINVAL;
		goto done;
	}
	vp = (struct vnode *)fp->f_data;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (vp->v_type == VDIR) {
		error = EISDIR;
		vn_unlock(vp);
		goto done;
	}

	if (vfs_quota_enabled) {
		error = VOP_GETATTR_FP(vp, &vattr, fp);
		KASSERT(error == 0, ("kern_ftruncate(): VOP_GETATTR didn't return 0"));
		uid = vattr.va_uid;
		gid = vattr.va_gid;
		old_size = vattr.va_size;
	}

	if ((error = vn_writechk(vp)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_size = length;
		error = VOP_SETATTR_FP(vp, &vattr, fp->f_cred, fp);
		mp = vq_vptomp(vp);
		VFS_ACCOUNT(mp, uid, gid, length - old_size);
	}
	vn_unlock(vp);
done:
	fdrop(fp);
	return (error);
}

/*
 * ftruncate_args(int fd, int pad, off_t length)
 *
 * Truncate a file given a file descriptor.
 */
int
sys_ftruncate(struct sysmsg *sysmsg, const struct ftruncate_args *uap)
{
	int error;

	error = kern_ftruncate(uap->fd, uap->length);

	return (error);
}

int
kern_fsync(int fd, bool fullsync)
{
	struct thread *td = curthread;
	struct vnode *vp;
	struct file *fp;
	vm_object_t obj;
	int error;

	if ((error = holdvnode(td, fd, &fp)) != 0)
		return (error);
	vp = (struct vnode *)fp->f_data;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if ((obj = vp->v_object) != NULL) {
		if (vp->v_mount == NULL ||
		    (vp->v_mount->mnt_kern_flag & MNTK_NOMSYNC) == 0) {
			vm_object_page_clean(obj, 0, 0, 0);
		}
	}
	error = fullsync ?
		VOP_FSYNC_FP(vp, MNT_WAIT, VOP_FSYNC_SYSCALL, fp) :
		VOP_FDATASYNC_FP(vp, MNT_WAIT, VOP_FSYNC_SYSCALL, fp);
	if (error == 0 && vp->v_mount)
		error = buf_fsync(vp);
	vn_unlock(vp);
	fdrop(fp);

	return (error);
}

/*
 * fsync(int fd)
 *
 * Sync an open file.
 */
int
sys_fsync(struct sysmsg *sysmsg, const struct fsync_args *uap)
{
	return (kern_fsync(uap->fd, true));
}

/*
 * fdatasync(int fd)
 *
 * Data-sync an open file.
 */
int
sys_fdatasync(struct sysmsg *sysmsg, const struct fdatasync_args *uap)
{
	return (kern_fsync(uap->fd, false));
}

/*
 * rename op.
 *
 * NOTE: error == 0 and nl_dvp is NULL indicates a mount point, operation
 *	 disallowed.  e.g. /var/cache where /var/cache is a null-mount, for
 *	 example.
 */
int
kern_rename(struct nlookupdata *fromnd, struct nlookupdata *tond)
{
	struct nchandle fnchd;
	struct nchandle tnchd;
	struct namecache *ncp;
	struct vnode *fdvp;
	struct vnode *tdvp;
	struct mount *mp;
	struct mount *userenlk;
	int error;
	u_int fncp_gen;
	u_int tncp_gen;

	bwillinode(1);
	fromnd->nl_flags |= NLC_REFDVP | NLC_RENAME_SRC;
	if ((error = nlookup(fromnd)) != 0)
		return (error);

	/*
	 * Attempt to rename a mount point (from or to)
	 */
	if (error == 0 && fromnd->nl_dvp == NULL)
		return (EINVAL);

	if ((fnchd.ncp = fromnd->nl_nch.ncp->nc_parent) == NULL)
		return (ENOENT);
	fnchd.mount = fromnd->nl_nch.mount;
	cache_hold(&fnchd);

	/*
	 * unlock the source nch so we can lookup the target nch without
	 * deadlocking.  The target may or may not exist so we do not check
	 * for a target vp like kern_mkdir() and other creation functions do.
	 *
	 * The source and target directories are ref'd and rechecked after
	 * everything is relocked to determine if the source or target file
	 * has been renamed.
	 */
	KKASSERT(fromnd->nl_flags & NLC_NCPISLOCKED);
	fromnd->nl_flags &= ~NLC_NCPISLOCKED;
	fncp_gen = fromnd->nl_nch.ncp->nc_generation;

	if (fromnd->nl_nch.ncp->nc_vp &&
	    fromnd->nl_nch.ncp->nc_vp->v_type == VDIR) {
		userenlk = fnchd.mount;
		cache_unlock(&fromnd->nl_nch);
		lockmgr(&userenlk->mnt_renlock, LK_EXCLUSIVE);
	} else {
		userenlk = NULL;
		cache_unlock(&fromnd->nl_nch);
	}

	/*
	 * Lookup target
	 */
	tond->nl_flags |= NLC_RENAME_DST | NLC_REFDVP;
	if ((error = nlookup(tond)) != 0) {
		cache_drop(&fnchd);
		goto done;
	}
	tncp_gen = tond->nl_nch.ncp->nc_generation;

	/*
	 * Attempt to rename a mount point (from or to)
	 */
	if (error == 0 && tond->nl_dvp == NULL) {
		cache_drop(&fnchd);
		error = ENOENT;
		goto done;
	}

	if ((tnchd.ncp = tond->nl_nch.ncp->nc_parent) == NULL) {
		cache_drop(&fnchd);
		error = ENOENT;
		goto done;
	}
	tnchd.mount = tond->nl_nch.mount;
	cache_hold(&tnchd);

	/*
	 * If the source and target are the same there is nothing to do
	 */
	if (fromnd->nl_nch.ncp == tond->nl_nch.ncp) {
		cache_drop(&fnchd);
		cache_drop(&tnchd);
		error = 0;
		goto done;
	}

	/*
	 * Mount points cannot be renamed or overwritten
	 */
	if ((fromnd->nl_nch.ncp->nc_flag | tond->nl_nch.ncp->nc_flag) &
	    NCF_ISMOUNTPT
	) {
		cache_drop(&fnchd);
		cache_drop(&tnchd);
		error = EINVAL;
		goto done;
	}

	/*
	 * Lock all four namecache entries.  tond is already locked.
	 */
	cache_lock4_tondlocked(&fnchd, &fromnd->nl_nch,
			       &tnchd, &tond->nl_nch,
			       fromnd->nl_cred, tond->nl_cred);
	fromnd->nl_flags |= NLC_NCPISLOCKED;

	/*
	 * If the namecache generation changed for either fromnd or tond,
	 * we must retry.
	 */
	if (((fromnd->nl_nch.ncp->nc_generation - fncp_gen) & ~1) ||
	    ((tond->nl_nch.ncp->nc_generation - tncp_gen) & ~1))
	{
		krateprintf(&krate_rename,
			"kern_rename: retry due to race on: "
			"\"%s\" -> \"%s\" (%d,%d)\n",
			fromnd->nl_nch.ncp->nc_name,
			tond->nl_nch.ncp->nc_name,
			fromnd->nl_nch.ncp->nc_generation - fncp_gen,
			tond->nl_nch.ncp->nc_generation - tncp_gen);
		error = EAGAIN;
		goto finish;
	}

	/*
	 * If either fromnd or tond are marked destroyed a ripout occured
	 * out from under us and we must retry.
	 */
	if ((fromnd->nl_nch.ncp->nc_flag & (NCF_DESTROYED | NCF_UNRESOLVED)) ||
	    fromnd->nl_nch.ncp->nc_vp == NULL ||
	    (tond->nl_nch.ncp->nc_flag & (NCF_DESTROYED | NCF_UNRESOLVED))) {
		krateprintf(&krate_rename,
			"kern_rename: retry due to ripout on: "
			"\"%s\" -> \"%s\"\n",
			fromnd->nl_nch.ncp->nc_name,
			tond->nl_nch.ncp->nc_name);
		error = EAGAIN;
		goto finish;
	}

	/*
	 * Make sure the parent directories linkages are the same.  We have
	 * already checked that fromnd and tond are not mount points so this
	 * should not loop forever on a cross-mount.
	 */
	if (fnchd.ncp != fromnd->nl_nch.ncp->nc_parent ||
	    tnchd.ncp != tond->nl_nch.ncp->nc_parent) {
		error = EAGAIN;
		goto finish;
	}

	/*
	 * Both the source and target must be within the same filesystem and
	 * in the same filesystem as their parent directories within the
	 * namecache topology.
	 *
	 * NOTE: fromnd's nc_mount or nc_vp could be NULL.
	 */
	mp = fnchd.mount;
	if (mp != tnchd.mount || mp != fromnd->nl_nch.mount ||
	    mp != tond->nl_nch.mount) {
		error = EXDEV;
		goto finish;
	}

	/*
	 * Make sure the mount point is writable
	 */
	if ((error = ncp_writechk(&tond->nl_nch)) != 0) {
		goto finish;
	}

	/*
	 * If the target exists and either the source or target is a directory,
	 * then both must be directories.
	 *
	 * Due to relocking of the source, fromnd->nl_nch.ncp->nc_vp might h
	 * have become NULL.
	 */
	if (tond->nl_nch.ncp->nc_vp) {
		if (fromnd->nl_nch.ncp->nc_vp == NULL) {
			error = ENOENT;
		} else if (fromnd->nl_nch.ncp->nc_vp->v_type == VDIR) {
			if (tond->nl_nch.ncp->nc_vp->v_type != VDIR)
				error = ENOTDIR;
		} else if (tond->nl_nch.ncp->nc_vp->v_type == VDIR) {
			error = EISDIR;
		}
	}

	/*
	 * You cannot rename a source into itself or a subdirectory of itself.
	 * We check this by travsersing the target directory upwards looking
	 * for a match against the source.
	 *
	 * Only required when renaming a directory, in which case userenlk is
	 * non-NULL.
	 */
	if (__predict_false(userenlk && error == 0)) {
		for (ncp = tnchd.ncp; ncp; ncp = ncp->nc_parent) {
			if (fromnd->nl_nch.ncp == ncp) {
				error = EINVAL;
				break;
			}
		}
	}

	/*
	 * Even though the namespaces are different, they may still represent
	 * hardlinks to the same file.  The filesystem might have a hard time
	 * with this so we issue a NREMOVE of the source instead of a NRENAME
	 * when we detect the situation.
	 */
	if (error == 0) {
		fdvp = fromnd->nl_dvp;
		tdvp = tond->nl_dvp;
		if (fdvp == NULL || tdvp == NULL) {
			error = EPERM;
		} else if (fromnd->nl_nch.ncp->nc_vp == tond->nl_nch.ncp->nc_vp) {
			error = VOP_NREMOVE(&fromnd->nl_nch, fdvp,
					    fromnd->nl_cred);
		} else {
			error = VOP_NRENAME(&fromnd->nl_nch, &tond->nl_nch, 
					    fdvp, tdvp, tond->nl_cred);
		}
	}
finish:
	cache_put(&tnchd);
	cache_put(&fnchd);
done:
	if (userenlk)
		lockmgr(&userenlk->mnt_renlock, LK_RELEASE);
	return (error);
}

/*
 * rename_args(char *from, char *to)
 *
 * Rename files.  Source and destination must either both be directories,
 * or both not be directories.  If target is a directory, it must be empty.
 */
int
sys_rename(struct sysmsg *sysmsg, const struct rename_args *uap)
{
	struct nlookupdata fromnd, tond;
	int error;

	do {
		error = nlookup_init(&fromnd, uap->from, UIO_USERSPACE, 0);
		if (error == 0) {
			error = nlookup_init(&tond, uap->to, UIO_USERSPACE, 0);
			if (error == 0)
				error = kern_rename(&fromnd, &tond);
			nlookup_done(&tond);
		}
		nlookup_done(&fromnd);
	} while (error == EAGAIN);
	return (error);
}

/*
 * renameat_args(int oldfd, char *old, int newfd, char *new)
 *
 * Rename files using paths relative to the directories associated with
 * oldfd and newfd.  Source and destination must either both be directories,
 * or both not be directories.  If target is a directory, it must be empty.
 */
int
sys_renameat(struct sysmsg *sysmsg, const struct renameat_args *uap)
{
	struct nlookupdata oldnd, newnd;
	struct file *oldfp, *newfp;
	int error;

	do {
		error = nlookup_init_at(&oldnd, &oldfp,
					uap->oldfd, uap->old,
					UIO_USERSPACE, 0);
		if (error == 0) {
			error = nlookup_init_at(&newnd, &newfp,
						uap->newfd, uap->new,
						UIO_USERSPACE, 0);
			if (error == 0)
				error = kern_rename(&oldnd, &newnd);
			nlookup_done_at(&newnd, newfp);
		}
		nlookup_done_at(&oldnd, oldfp);
	} while (error == EAGAIN);
	return (error);
}

int
kern_mkdir(struct nlookupdata *nd, int mode)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct vattr vattr;
	int error;

	bwillinode(1);
	nd->nl_flags |= NLC_WILLBEDIR | NLC_CREATE | NLC_REFDVP;
	if ((error = nlookup(nd)) != 0)
		return (error);

	if (nd->nl_nch.ncp->nc_vp)
		return (EEXIST);
	if (nd->nl_dvp == NULL)
		return (EINVAL);
	if ((error = ncp_writechk(&nd->nl_nch)) != 0)
		return (error);
	VATTR_NULL(&vattr);
	vattr.va_type = VDIR;
	vattr.va_mode = (mode & ACCESSPERMS) &~ p->p_fd->fd_cmask;

	vp = NULL;
	error = VOP_NMKDIR(&nd->nl_nch, nd->nl_dvp, &vp, td->td_ucred, &vattr);
	if (error == 0)
		vput(vp);
	return (error);
}

/*
 * mkdir_args(char *path, int mode)
 *
 * Make a directory file.
 */
int
sys_mkdir(struct sysmsg *sysmsg, const struct mkdir_args *uap)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_mkdir(&nd, uap->mode);
	nlookup_done(&nd);
	return (error);
}

/*
 * mkdirat_args(int fd, char *path, mode_t mode)
 *
 * Make a directory file.  The path is relative to the directory associated
 * with fd.
 */
int
sys_mkdirat(struct sysmsg *sysmsg, const struct mkdirat_args *uap)
{
	struct nlookupdata nd;
	struct file *fp;
	int error;

	error = nlookup_init_at(&nd, &fp, uap->fd, uap->path, UIO_USERSPACE, 0);
	if (error == 0)
		error = kern_mkdir(&nd, uap->mode);
	nlookup_done_at(&nd, fp);
	return (error);
}

int
kern_rmdir(struct nlookupdata *nd)
{
	int error;

	bwillinode(1);
	nd->nl_flags |= NLC_DELETE | NLC_REFDVP;
	if ((error = nlookup(nd)) != 0)
		return (error);

	/*
	 * Do not allow directories representing mount points to be
	 * deleted, even if empty.  Check write perms on mount point
	 * in case the vnode is aliased (aka nullfs).
	 */
	if (nd->nl_nch.ncp->nc_flag & (NCF_ISMOUNTPT))
		return (EBUSY);
	if (nd->nl_dvp == NULL)
		return (EINVAL);
	if ((error = ncp_writechk(&nd->nl_nch)) != 0)
		return (error);
	error = VOP_NRMDIR(&nd->nl_nch, nd->nl_dvp, nd->nl_cred);
	return (error);
}

/*
 * rmdir_args(char *path)
 *
 * Remove a directory file.
 */
int
sys_rmdir(struct sysmsg *sysmsg, const struct rmdir_args *uap)
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
kern_getdirentries(int fd, char *buf, u_int count, long *basep, int *res,
		   enum uio_seg direction)
{
	struct thread *td = curthread;
	struct vnode *vp;
	struct file *fp;
	struct uio auio;
	struct iovec aiov;
	off_t loff;
	int error, eofflag;

	if ((error = holdvnode(td, fd, &fp)) != 0)
		return (error);
	if ((fp->f_flag & FREAD) == 0) {
		error = EBADF;
		goto done;
	}
	vp = (struct vnode *)fp->f_data;
	if (vp->v_type != VDIR) {
		error = EINVAL;
		goto done;
	}
	aiov.iov_base = buf;
	aiov.iov_len = count;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = direction;
	auio.uio_td = td;
	auio.uio_resid = count;
	loff = auio.uio_offset = fp->f_offset;
	error = VOP_READDIR_FP(vp, &auio, fp->f_cred, &eofflag, NULL, NULL, fp);
	fp->f_offset = auio.uio_offset;
	if (error)
		goto done;

	/*
	 * WARNING!  *basep may not be wide enough to accomodate the
	 * seek offset.   XXX should we hack this to return the upper 32 bits
	 * for offsets greater then 4G?
	 */
	if (basep) {
		*basep = (long)loff;
	}
	*res = count - auio.uio_resid;
done:
	fdrop(fp);
	return (error);
}

/*
 * getdirentries_args(int fd, char *buf, u_int conut, long *basep)
 *
 * Read a block of directory entries in a file system independent format.
 */
int
sys_getdirentries(struct sysmsg *sysmsg, const struct getdirentries_args *uap)
{
	long base;
	int error;

	error = kern_getdirentries(uap->fd, uap->buf, uap->count, &base,
				   &sysmsg->sysmsg_result, UIO_USERSPACE);

	if (error == 0 && uap->basep)
		error = copyout(&base, uap->basep, sizeof(*uap->basep));
	return (error);
}

/*
 * getdents_args(int fd, char *buf, size_t count)
 */
int
sys_getdents(struct sysmsg *sysmsg, const struct getdents_args *uap)
{
	int error;

	error = kern_getdirentries(uap->fd, uap->buf, uap->count, NULL,
				   &sysmsg->sysmsg_result, UIO_USERSPACE);

	return (error);
}

/*
 * Set the mode mask for creation of filesystem nodes.
 *
 * umask(int newmask)
 */
int
sys_umask(struct sysmsg *sysmsg, const struct umask_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp;

	fdp = p->p_fd;
	sysmsg->sysmsg_result = fdp->fd_cmask;
	fdp->fd_cmask = uap->newmask & ALLPERMS;
	return (0);
}

/*
 * revoke(char *path)
 *
 * Void all references to file by ripping underlying filesystem
 * away from vnode.
 */
int
sys_revoke(struct sysmsg *sysmsg, const struct revoke_args *uap)
{
	struct nlookupdata nd;
	struct vattr vattr;
	struct vnode *vp;
	struct ucred *cred;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
	cred = crhold(nd.nl_cred);
	nlookup_done(&nd);
	if (error == 0) {
		if (error == 0)
			error = VOP_GETATTR(vp, &vattr);
		if (error == 0 && cred->cr_uid != vattr.va_uid)
			error = priv_check_cred(cred, PRIV_VFS_REVOKE, 0);
		if (error == 0 && (vp->v_type == VCHR || vp->v_type == VBLK)) {
			if (vcount(vp) > 0)
				error = vrevoke(vp, cred);
		} else if (error == 0) {
			error = vrevoke(vp, cred);
		}
		vrele(vp);
	}
	if (cred)
		crfree(cred);
	return (error);
}

/*
 * getfh_args(char *fname, fhandle_t *fhp)
 *
 * Get (NFS) file handle
 *
 * NOTE: We use the fsid of the covering mount, even if it is a nullfs
 * mount.  This allows nullfs mounts to be explicitly exported. 
 *
 * WARNING: nullfs mounts of HAMMER PFS ROOTs are safe.
 *
 * 	    nullfs mounts of subdirectories are not safe.  That is, it will
 *	    work, but you do not really have protection against access to
 *	    the related parent directories.
 */
int
sys_getfh(struct sysmsg *sysmsg, const struct getfh_args *uap)
{
	struct thread *td = curthread;
	struct nlookupdata nd;
	fhandle_t fh;
	struct vnode *vp;
	struct mount *mp;
	int error;

	/*
	 * Must be super user
	 */
	if ((error = priv_check(td, PRIV_ROOT)) != 0)
		return (error);

	vp = NULL;
	error = nlookup_init(&nd, uap->fname, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(&nd.nl_nch, nd.nl_cred, LK_EXCLUSIVE, &vp);
	mp = nd.nl_nch.mount;
	nlookup_done(&nd);
	if (error == 0) {
		bzero(&fh, sizeof(fh));
		fh.fh_fsid = mp->mnt_stat.f_fsid;
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
 * warning: do not remove the priv_check() call or this becomes one giant
 * security hole.
 */
int
sys_fhopen(struct sysmsg *sysmsg, const struct fhopen_args *uap)
{
	struct thread *td = curthread;
	struct filedesc *fdp = td->td_proc->p_fd;
	struct mount *mp;
	struct vnode *vp;
	struct fhandle fhp;
	struct vattr vat;
	struct vattr *vap = &vat;
	struct flock lf;
	int fmode, mode, error = 0, type;
	struct file *nfp; 
	struct file *fp;
	int indx;

	/*
	 * Must be super user
	 */
	error = priv_check(td, PRIV_ROOT);
	if (error)
		return (error);

	fmode = FFLAGS(uap->flags);

	/*
	 * Why not allow a non-read/write open for our lockd?
	 */
	if (((fmode & (FREAD | FWRITE)) == 0) || (fmode & O_CREAT))
		return (EINVAL);
	error = copyin(uap->u_fhp, &fhp, sizeof(fhp));
	if (error)
		return(error);

	/*
	 * Find the mount point
	 */
	mp = vfs_getvfs(&fhp.fh_fsid);
	if (mp == NULL) {
		error = ESTALE;
		goto done2;
	}
	/* now give me my vnode, it gets returned to me locked */
	error = VFS_FHTOVP(mp, NULL, &fhp.fh_fid, &vp);
	if (error)
		goto done;
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
		error = VOP_ACCESS(vp, mode, td->td_ucred);
		if (error)
			goto bad;
	}
	if (fmode & O_TRUNC) {
		vn_unlock(vp);				/* XXX */
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);	/* XXX */
		VATTR_NULL(vap);
		vap->va_size = 0;
		error = VOP_SETATTR(vp, vap, td->td_ucred);
		if (error)
			goto bad;
	}

	/*
	 * VOP_OPEN needs the file pointer so it can potentially override
	 * it.
	 *
	 * WARNING! no f_nchandle will be associated when fhopen()ing a
	 * directory.  XXX
	 */
	if ((error = falloc(td->td_lwp, &nfp, &indx)) != 0)
		goto bad;
	error = VOP_OPEN(vp, fmode, td->td_ucred, &nfp);
	fp = nfp;

	if (error) {
		/*
		 * setting f_ops this way prevents VOP_CLOSE from being
		 * called or fdrop() releasing the vp from v_data.   Since
		 * the VOP_OPEN failed we don't want to VOP_CLOSE.
		 */
		fp->f_ops = &badfileops;
		fp->f_data = NULL;
		goto bad_drop;
	}

	/*
	 * The fp is given its own reference, we still have our ref and lock.
	 *
	 * Assert that all regular files must be created with a VM object.
	 */
	if (vp->v_type == VREG && vp->v_object == NULL) {
		kprintf("fhopen: regular file did not "
			"have VM object: %p\n",
			vp);
		goto bad_drop;
	}

	/*
	 * The open was successful.  Handle any locking requirements.
	 */
	if (fmode & (O_EXLOCK | O_SHLOCK)) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		if (fmode & O_EXLOCK)
			lf.l_type = F_WRLCK;
		else
			lf.l_type = F_RDLCK;
		if (fmode & FNONBLOCK)
			type = 0;
		else
			type = F_WAIT;
		vn_unlock(vp);
		if ((error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK,
					 &lf, type)) != 0) {
			/*
			 * release our private reference.
			 */
			fsetfd(fdp, NULL, indx);
			fdrop(fp);
			vrele(vp);
			goto done;
		}
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		atomic_set_int(&fp->f_flag, FHASLOCK);	/* race ok */
	}

	/*
	 * Clean up.  Associate the file pointer with the previously
	 * reserved descriptor and return it.
	 */
	vput(vp);
	if (uap->flags & O_CLOEXEC)
		fdp->fd_files[indx].fileflags |= UF_EXCLOSE;
	fsetfd(fdp, fp, indx);
	fdrop(fp);
	sysmsg->sysmsg_result = indx;
	mount_drop(mp);

	return (error);

bad_drop:
	fsetfd(fdp, NULL, indx);
	fdrop(fp);
bad:
	vput(vp);
done:
	mount_drop(mp);
done2:
	return (error);
}

/*
 * fhstat_args(struct fhandle *u_fhp, struct stat *sb)
 */
int
sys_fhstat(struct sysmsg *sysmsg, const struct fhstat_args *uap)
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
	error = priv_check(td, PRIV_ROOT);
	if (error)
		return (error);
	
	error = copyin(uap->u_fhp, &fh, sizeof(fhandle_t));
	if (error)
		return (error);

	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL)
		error = ESTALE;
	if (error == 0) {
		if ((error = VFS_FHTOVP(mp, NULL, &fh.fh_fid, &vp)) == 0) {
			error = vn_stat(vp, &sb, td->td_ucred);
			vput(vp);
		}
	}
	if (error == 0)
		error = copyout(&sb, uap->sb, sizeof(sb));
	if (mp)
		mount_drop(mp);

	return (error);
}

/*
 * fhstatfs_args(struct fhandle *u_fhp, struct statfs *buf)
 */
int
sys_fhstatfs(struct sysmsg *sysmsg, const struct fhstatfs_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct statfs *sp;
	struct mount *mp;
	struct vnode *vp;
	struct statfs sb;
	char *fullpath, *freepath;
	fhandle_t fh;
	int error;

	/*
	 * Must be super user
	 */
	if ((error = priv_check(td, PRIV_ROOT)))
		return (error);

	if ((error = copyin(uap->u_fhp, &fh, sizeof(fhandle_t))) != 0)
		return (error);

	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL) {
		error = ESTALE;
		goto done;
	}
	if (p != NULL && !chroot_visible_mnt(mp, p)) {
		error = ESTALE;
		goto done;
	}

	if ((error = VFS_FHTOVP(mp, NULL, &fh.fh_fid, &vp)) != 0)
		goto done;
	mp = vp->v_mount;
	sp = &mp->mnt_stat;
	vput(vp);
	if ((error = VFS_STATFS(mp, sp, td->td_ucred)) != 0)
		goto done;

	error = mount_path(p, mp, &fullpath, &freepath);
	if (error)
		goto done;
	bzero(sp->f_mntonname, sizeof(sp->f_mntonname));
	strlcpy(sp->f_mntonname, fullpath, sizeof(sp->f_mntonname));
	kfree(freepath, M_TEMP);

	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	if (priv_check(td, PRIV_ROOT)) {
		bcopy(sp, &sb, sizeof(sb));
		sb.f_fsid.val[0] = sb.f_fsid.val[1] = 0;
		sp = &sb;
	}
	error = copyout(sp, uap->buf, sizeof(*sp));
done:
	if (mp)
		mount_drop(mp);

	return (error);
}

/*
 * fhstatvfs_args(struct fhandle *u_fhp, struct statvfs *buf)
 */
int
sys_fhstatvfs(struct sysmsg *sysmsg, const struct fhstatvfs_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct statvfs *sp;
	struct mount *mp;
	struct vnode *vp;
	fhandle_t fh;
	int error;

	/*
	 * Must be super user
	 */
	if ((error = priv_check(td, PRIV_ROOT)))
		return (error);

	if ((error = copyin(uap->u_fhp, &fh, sizeof(fhandle_t))) != 0)
		return (error);

	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL) {
		error = ESTALE;
		goto done;
	}
	if (p != NULL && !chroot_visible_mnt(mp, p)) {
		error = ESTALE;
		goto done;
	}

	if ((error = VFS_FHTOVP(mp, NULL, &fh.fh_fid, &vp)))
		goto done;
	mp = vp->v_mount;
	sp = &mp->mnt_vstat;
	vput(vp);
	if ((error = VFS_STATVFS(mp, sp, td->td_ucred)) != 0)
		goto done;

	sp->f_flag = 0;
	if (mp->mnt_flag & MNT_RDONLY)
		sp->f_flag |= ST_RDONLY;
	if (mp->mnt_flag & MNT_NOSUID)
		sp->f_flag |= ST_NOSUID;
	error = copyout(sp, uap->buf, sizeof(*sp));
done:
	if (mp)
		mount_drop(mp);
	return (error);
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
sys_extattrctl(struct sysmsg *sysmsg, const struct extattrctl_args *uap)
{
	struct nlookupdata nd;
	struct vnode *vp;
	char attrname[EXTATTR_MAXNAMELEN];
	int error;
	size_t size;

	attrname[0] = 0;
	vp = NULL;
	error = 0;

	if (error == 0 && uap->filename) {
		error = nlookup_init(&nd, uap->filename, UIO_USERSPACE,
				     NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&nd);
		if (error == 0)
			error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
		nlookup_done(&nd);
	}

	if (error == 0 && uap->attrname) {
		error = copyinstr(uap->attrname, attrname, EXTATTR_MAXNAMELEN,
				  &size);
	}

	if (error == 0) {
		error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&nd);
		if (error == 0)
			error = ncp_writechk(&nd.nl_nch);
		if (error == 0) {
			error = VFS_EXTATTRCTL(nd.nl_nch.mount, uap->cmd, vp,
					       uap->attrnamespace,
					       uap->attrname, nd.nl_cred);
		}
		nlookup_done(&nd);
	}

	return (error);
}

/*
 * Syscall to get a named extended attribute on a file or directory.
 */
int
sys_extattr_set_file(struct sysmsg *sysmsg,
		     const struct extattr_set_file_args *uap)
{
	char attrname[EXTATTR_MAXNAMELEN];
	struct nlookupdata nd;
	struct vnode *vp;
	struct uio auio;
	struct iovec aiov;
	int error;

	error = copyin(uap->attrname, attrname, EXTATTR_MAXNAMELEN);
	if (error)
		return (error);

	vp = NULL;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = ncp_writechk(&nd.nl_nch);
	if (error == 0)
		error = cache_vget(&nd.nl_nch, nd.nl_cred, LK_EXCLUSIVE, &vp);
	if (error) {
		nlookup_done(&nd);
		return (error);
	}

	bzero(&auio, sizeof(auio));
	aiov.iov_base = uap->data;
	aiov.iov_len = uap->nbytes;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = uap->nbytes;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = curthread;

	error = VOP_SETEXTATTR(vp, uap->attrnamespace, attrname,
			       &auio, nd.nl_cred);

	vput(vp);
	nlookup_done(&nd);
	return (error);
}

/*
 * Syscall to get a named extended attribute on a file or directory.
 */
int
sys_extattr_get_file(struct sysmsg *sysmsg,
		     const struct extattr_get_file_args *uap)
{
	char attrname[EXTATTR_MAXNAMELEN];
	struct nlookupdata nd;
	struct uio auio;
	struct iovec aiov;
	struct vnode *vp;
	int error;

	error = copyin(uap->attrname, attrname, EXTATTR_MAXNAMELEN);
	if (error)
		return (error);

	vp = NULL;

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(&nd.nl_nch, nd.nl_cred, LK_SHARED, &vp);
	if (error) {
		nlookup_done(&nd);
		return (error);
	}

	bzero(&auio, sizeof(auio));
	aiov.iov_base = uap->data;
	aiov.iov_len = uap->nbytes;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = uap->nbytes;
	auio.uio_rw = UIO_READ;
	auio.uio_td = curthread;

	error = VOP_GETEXTATTR(vp, uap->attrnamespace, attrname,
				&auio, nd.nl_cred);
	sysmsg->sysmsg_result = uap->nbytes - auio.uio_resid;

	vput(vp);
	nlookup_done(&nd);
	return(error);
}

/*
 * Syscall to delete a named extended attribute from a file or directory.
 * Accepts attribute name.  The real work happens in VOP_SETEXTATTR().
 */
int
sys_extattr_delete_file(struct sysmsg *sysmsg,
			const struct extattr_delete_file_args *uap)
{
	char attrname[EXTATTR_MAXNAMELEN];
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	error = copyin(uap->attrname, attrname, EXTATTR_MAXNAMELEN);
	if (error)
		return(error);

	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = ncp_writechk(&nd.nl_nch);
	if (error == 0) {
		error = cache_vget(&nd.nl_nch, nd.nl_cred, LK_EXCLUSIVE, &vp);
		if (error == 0) {
			error = VOP_SETEXTATTR(vp, uap->attrnamespace,
					       attrname, NULL, nd.nl_cred);
			vput(vp);
		}
	}
	nlookup_done(&nd);
	return(error);
}

/*
 * Determine if the mount is visible to the process.
 */
static int
chroot_visible_mnt(struct mount *mp, struct proc *p)
{
	struct nchandle nch;

	/*
	 * Traverse from the mount point upwards.  If we hit the process
	 * root then the mount point is visible to the process.
	 */
	nch = mp->mnt_ncmountpt;
	while (nch.ncp) {
		if (nch.mount == p->p_fd->fd_nrdir.mount &&
		    nch.ncp == p->p_fd->fd_nrdir.ncp) {
			return(1);
		}
		if (nch.ncp == nch.mount->mnt_ncmountpt.ncp) {
			nch = nch.mount->mnt_ncmounton;
		} else {
			nch.ncp = nch.ncp->nc_parent;
		}
	}

	/*
	 * If the mount point is not visible to the process, but the
	 * process root is in a subdirectory of the mount, return
	 * TRUE anyway.
	 */
	if (p->p_fd->fd_nrdir.mount == mp)
		return(1);

	return(0);
}

/* Sets priv to PRIV_ROOT in case no matching fs */
static int
get_fspriv(const char *fsname)
{

	if (strncmp("null", fsname, 5) == 0) {
		return PRIV_VFS_MOUNT_NULLFS;
	} else if (strncmp(fsname, "tmpfs", 6) == 0) {
		return PRIV_VFS_MOUNT_TMPFS;
	}

	return PRIV_ROOT;
}

int
sys___realpath(struct sysmsg *sysmsg, const struct __realpath_args *uap)
{
	struct nlookupdata nd;
	char *rbuf;
	char *fbuf;
	ssize_t rlen;
	int error;

	/*
	 * Invalid length if less than 0.  0 is allowed
	 */
	if ((ssize_t)uap->len < 0)
		return EINVAL;

	rbuf = NULL;
	fbuf = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error)
		goto done;

	nd.nl_flags |= NLC_SHAREDLOCK;
	error = nlookup(&nd);
	if (error)
		goto done;

	if (nd.nl_nch.ncp->nc_vp == NULL) {
		error = ENOENT;
		goto done;
	}

	/*
	 * Shortcut test for existence.
	 */
	if (uap->len == 0) {
		error = ENAMETOOLONG;
		goto done;
	}

	/*
	 * Obtain the path relative to the process root.  The nch must not
	 * be locked for the cache_fullpath() call.
	 */
	if (nd.nl_flags & NLC_NCPISLOCKED) {
		nd.nl_flags &= ~NLC_NCPISLOCKED;
		cache_unlock(&nd.nl_nch);
	}
	error = cache_fullpath(curproc, &nd.nl_nch, NULL, &rbuf, &fbuf, 0);
	if (error)
		goto done;

	rlen = (ssize_t)strlen(rbuf);
	if (rlen >= uap->len) {
		error = ENAMETOOLONG;
		goto done;
	}
	error = copyout(rbuf, uap->buf, rlen + 1);
	if (error == 0)
		sysmsg->sysmsg_szresult = rlen;
done:
	nlookup_done(&nd);
	if (fbuf)
		kfree(fbuf, M_TEMP);

	return error;
}

int
sys_posix_fallocate(struct sysmsg *sysmsg, const struct posix_fallocate_args *uap)
{
	return (kern_posix_fallocate(uap->fd, uap->offset, uap->len));
}

int
kern_posix_fallocate(int fd, off_t offset, off_t len)
{
	struct thread *td = curthread;
	struct vnode *vp;
	struct file *fp;
	int error;

	if (offset < 0 || len <= 0)
		return (EINVAL);
	/* Check for wrap. */
	if (offset > OFF_MAX - len)
		return (EFBIG);

	fp = holdfp(td, fd, -1);
	if (fp == NULL)
		return (EBADF);

	switch (fp->f_type) {
	case DTYPE_VNODE:
		break;
	case DTYPE_PIPE:
	case DTYPE_FIFO:
		error = ESPIPE;
		goto out;
	default:
		error = ENODEV;
		goto out;
	}

	if ((fp->f_flag & FWRITE) == 0) {
		error = EBADF;
		goto out;
	}

	vp = fp->f_data;
	if (vp->v_type != VREG) {
		error = ENODEV;
		goto out;
	}

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_ALLOCATE(vp, offset, len);
	vn_unlock(vp);
out:
	dropfp(td, fd, fp);
	return (error);
}
