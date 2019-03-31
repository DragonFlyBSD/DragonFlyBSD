/*-
 * Copyright (c) 2019 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2019 The DragonFly Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "fuse.h"

#include <sys/device.h>
#include <sys/devfs.h>
#include <sys/nlookup.h>
#include <sys/file.h>
#include <sys/sysctl.h>
#include <sys/statvfs.h>
#include <sys/priv.h>

int fuse_debug = 0;

SYSCTL_NODE(_vfs, OID_AUTO, fuse, CTLFLAG_RD, 0, "FUSE");

SYSCTL_INT(_vfs_fuse, OID_AUTO, version_major, CTLFLAG_RD, NULL,
    FUSE_KERNEL_VERSION, "FUSE kernel version (major)");
SYSCTL_INT(_vfs_fuse, OID_AUTO, version_minor, CTLFLAG_RD, NULL,
    FUSE_KERNEL_MINOR_VERSION, "FUSE kernel version (minor)");

SYSCTL_INT(_vfs_fuse, OID_AUTO, debug, CTLFLAG_RW, &fuse_debug, 1, "");

int
fuse_cmp_version(struct fuse_mount *fmp, uint32_t major, uint32_t minor)
{
	if (fmp->abi_major == major && fmp->abi_minor == minor)
		return 0;

	if (fmp->abi_major > major ||
	    (fmp->abi_major == major && fmp->abi_minor > minor))
		return 1;

	return -1;
}

int
fuse_mount_kill(struct fuse_mount *fmp)
{
	if (!fuse_test_dead(fmp)) {
		fuse_set_dead(fmp);
		wakeup(fmp);
		KNOTE(&fmp->kq.ki_note, 0);
		return 0;
	}

	return -1;
}

int
fuse_mount_free(struct fuse_mount *fmp)
{
	if (refcount_release(&fmp->refcnt)) {
		fuse_dbg("fmp=%p free\n", fmp);
		mtx_uninit(&fmp->ipc_lock);
		mtx_uninit(&fmp->mnt_lock);
		crfree(fmp->cred);
		kfree(fmp, M_TEMP);
		return 0;
	}
	fuse_dbg("fmp=%p %u refcnt left\n", fmp, fmp->refcnt);

	return -1;
}

static int
fuse_mount(struct mount *mp, char *mntpt, caddr_t data, struct ucred *cred)
{
	struct statfs *sbp = &mp->mnt_stat;
	struct vnode *devvp;
	struct file *file;
	struct nlookupdata nd;
	struct fuse_mount_info args;
	struct fuse_mount *fmp;
	struct fuse_ipc *fip;
	struct fuse_init_in *fii;
	struct fuse_init_out *fio;
	char subtype[512];
	int error;

	if (mp->mnt_flag & MNT_UPDATE)
		return EOPNOTSUPP;

	error = copyin(data, &args, sizeof(args));
	if (error)
		return error;
	memcpy(sbp->f_mntfromname, args.from, sizeof(sbp->f_mntfromname));

	memset(sbp->f_mntfromname, 0, sizeof(sbp->f_mntfromname));
	error = copyinstr(args.from, sbp->f_mntfromname,
	    sizeof(sbp->f_mntfromname), NULL);
	if (error)
		return error;

	memset(sbp->f_mntonname, 0, sizeof(sbp->f_mntonname));
	error = copyinstr(mntpt, sbp->f_mntonname, sizeof(sbp->f_mntonname),
	    NULL);
	if (error)
		return error;

	memset(subtype, 0, sizeof(subtype));
	error = copyinstr(args.subtype, subtype, sizeof(subtype), NULL);
	if (error)
		return error;
	if (strlen(subtype)) {
		strlcat(sbp->f_fstypename, ".", sizeof(sbp->f_fstypename));
		strlcat(sbp->f_fstypename, subtype, sizeof(sbp->f_fstypename));
	}

	error = nlookup_init(&nd, sbp->f_mntfromname, UIO_SYSSPACE, NLC_FOLLOW);
	if (!error) {
		error = nlookup(&nd);
		if (!error)
			error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
		nlookup_done(&nd);
	}
	if (error)
		return error;
	if (!devvp)
		return ENODEV;

	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_ACCESS(devvp, VREAD | VWRITE, cred);
	if (error)
		error = priv_check_cred(cred, PRIV_ROOT, 0);
	if (error) {
		vput(devvp);
		return error;
	}
	vn_unlock(devvp);

	fuse_dbg("fd=%d\n", args.fd);
	file = holdfp_fdp(curthread->td_proc->p_fd, args.fd, FREAD | FWRITE);
	if (!file) {
		vrele(devvp);
		return EBADF;
	}
	error = devfs_get_cdevpriv(file, (void**)&fmp);
	dropfp(curthread, args.fd, file);
	if (error) {
		vrele(devvp);
		return error;
	}
	KKASSERT(fmp);

	fmp->mp = mp;
	fmp->dead = false;
	mtx_init(&fmp->mnt_lock, "fuse_mnt_lock");
	mtx_init(&fmp->ipc_lock, "fuse_ipc_lock");
	TAILQ_INIT(&fmp->request_head);
	TAILQ_INIT(&fmp->reply_head);
	fmp->devvp = devvp;
	fmp->cred = crhold(cred);
	KKASSERT(fmp->refcnt > 0);
	refcount_acquire(&fmp->refcnt);

	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;
	mp->mnt_data = (qaddr_t)fmp;

	fuse_node_new(fmp, FUSE_ROOT_ID, VDIR, &fmp->rfnp);
	KKASSERT(fmp->rfnp->ino == FUSE_ROOT_ID);

	vfs_getnewfsid(mp);
	vfs_add_vnodeops(mp, &fuse_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &fuse_spec_vops, &mp->mnt_vn_spec_ops);

	fip = fuse_ipc_get(fmp, sizeof(*fii));
	fii = fuse_ipc_fill(fip, FUSE_INIT, FUSE_ROOT_ID, NULL);
	fii->major = FUSE_KERNEL_VERSION;
	fii->minor = FUSE_KERNEL_MINOR_VERSION;
	fii->max_readahead = FUSE_BLKSIZE;
	/* unused */
	//fii->flags = ...;

	error = fuse_ipc_tx(fip);
	if (error) {
		vrele(devvp);
		return error;
	}

	fio = fuse_out_data(fip);
	fmp->abi_major = fio->major;
	fmp->abi_minor = fio->minor;
	fmp->max_write = fio->max_write;

	if (fuse_cmp_version(fmp, 7, 0) < 0) {
		fuse_ipc_put(fip);
		vrele(devvp);
		return EPROTONOSUPPORT;
	}

	/* unused */
	//fio->max_readahead
	//fio->flags
	//fio->max_background
	//fio->congestion_threshold
	//fio->time_gran
	//fio->max_pages
	fuse_print("FUSE UABI %d.%d\n", fmp->abi_major, fmp->abi_minor);

	fuse_ipc_put(fip);

	VFS_STATFS(mp, &mp->mnt_stat, cred);

	return 0;
}

static int
fuse_unmount(struct mount *mp, int mntflags)
{
	struct fuse_mount *fmp = VFSTOFUSE(mp);
	struct fuse_ipc *fip;
	int error, flags = 0;

	mtx_lock(&fmp->mnt_lock);
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	error = vflush(mp, 0, flags);
	if (error) {
		mtx_unlock(&fmp->mnt_lock);
		fuse_dbg("vflush error=%d\n", error);
		return error;
	}

	if (!fuse_test_dead(fmp)) {
		fuse_dbg("not dead yet, destroying\n");
		fip = fuse_ipc_get(fmp, 0);
		fuse_ipc_fill(fip, FUSE_DESTROY, FUSE_ROOT_ID, NULL);
		if (!fuse_ipc_tx(fip))
			fuse_ipc_put(fip);
		fuse_mount_kill(fmp);
	}

	/* The userspace fs will exit anyway after FUSE_DESTROY. */
	vn_lock(fmp->devvp, LK_EXCLUSIVE | LK_RETRY);
	VOP_CLOSE(fmp->devvp, FREAD | FWRITE, NULL);
	vn_unlock(fmp->devvp);

	vrele(fmp->devvp);
	mtx_unlock(&fmp->mnt_lock);

	fuse_mount_free(fmp);
	mp->mnt_data = NULL;
	mp->mnt_flag &= ~MNT_LOCAL;

	fuse_dbg("unmount done\n");

	return 0;
}

static int
fuse_root(struct mount *mp, struct vnode **vpp)
{
	struct fuse_mount *fmp = VFSTOFUSE(mp);
	int error;

	KASSERT(fmp->rfnp, ("no root node"));
	KKASSERT(fmp->rfnp->fmp);

	error = fuse_node_vn(fmp->rfnp, LK_EXCLUSIVE, vpp);
	if (!error) {
		struct vnode *vp = *vpp;
		vp->v_flag |= VROOT;
		KKASSERT(vp->v_type == VDIR);
	}

	return error;
}

static int
fuse_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct fuse_mount *fmp = VFSTOFUSE(mp);
	struct fuse_ipc *fip;
	struct fuse_statfs_out *fso;
	int error;

	fip = fuse_ipc_get(fmp, 0);
	fuse_ipc_fill(fip, FUSE_STATFS, FUSE_ROOT_ID, cred);
	error = fuse_ipc_tx(fip);
	if (error)
		return error;

	fso = fuse_out_data(fip);

	mtx_lock(&fmp->mnt_lock);
	sbp->f_bsize = fso->st.frsize;
	sbp->f_iosize = FUSE_BLKSIZE;
	sbp->f_blocks = fso->st.blocks;
	sbp->f_bfree = fso->st.bfree;
	sbp->f_bavail = fso->st.bavail;
	sbp->f_files = fso->st.files;
	sbp->f_ffree = fso->st.ffree;
	mtx_unlock(&fmp->mnt_lock);

	fuse_ipc_put(fip);

	return 0;
}

static int
fuse_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	struct fuse_mount *fmp = VFSTOFUSE(mp);
	struct fuse_ipc *fip;
	struct fuse_statfs_out *fso;
	int error;

	fip = fuse_ipc_get(fmp, 0);
	fuse_ipc_fill(fip, FUSE_STATFS, FUSE_ROOT_ID, cred);
	error = fuse_ipc_tx(fip);
	if (error)
		return error;

	fso = fuse_out_data(fip);

	mtx_lock(&fmp->mnt_lock);
	sbp->f_bsize = fso->st.frsize;
	sbp->f_frsize = FUSE_BLKSIZE;
	sbp->f_blocks = fso->st.blocks;
	sbp->f_bfree = fso->st.bfree;
	sbp->f_bavail = fso->st.bavail;
	sbp->f_files = fso->st.files;
	sbp->f_ffree = fso->st.ffree;
	mtx_unlock(&fmp->mnt_lock);

	fuse_ipc_put(fip);

	return 0;
}

static int
fuse_init(struct vfsconf *vfsp)
{
	int error;

	fuse_node_init();
	fuse_ipc_init();
	fuse_file_init();

	error = fuse_device_init();
	if (error) {
		fuse_file_cleanup();
		fuse_ipc_cleanup();
		fuse_node_cleanup();
		return error;
	}

	fuse_print("FUSE ABI %d.%d\n", FUSE_KERNEL_VERSION,
	    FUSE_KERNEL_MINOR_VERSION);

	return 0;
}

static int
fuse_uninit(struct vfsconf *vfsp)
{
	fuse_file_cleanup();
	fuse_ipc_cleanup();
	fuse_node_cleanup();
	fuse_device_cleanup();

	return 0;
}

static struct vfsops fuse_vfsops = {
	.vfs_init = fuse_init,
	.vfs_uninit = fuse_uninit,
	.vfs_mount = fuse_mount,
	.vfs_unmount = fuse_unmount,
	.vfs_root = fuse_root,
	.vfs_statfs = fuse_statfs,
	.vfs_statvfs = fuse_statvfs,
};

VFS_SET(fuse_vfsops, fuse, VFCF_SYNTHETIC | VFCF_MPSAFE);
MODULE_VERSION(fuse, 1);
