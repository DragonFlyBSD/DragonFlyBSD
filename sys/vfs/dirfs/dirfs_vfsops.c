/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Antonio Huete Jimenez <tuxillo@quantumachine.net>
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
 */

#include <sys/vfsops.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/queue.h>
#include <sys/spinlock2.h>
#include <sys/sysref2.h>
#include <sys/ktr.h>

#include <string.h>

#include "dirfs.h"

MALLOC_DEFINE(M_DIRFS, "dirfs", "dirfs mount allocation");
MALLOC_DEFINE(M_DIRFS_NODE, "dirfs nodes", "dirfs nodes memory allocation");
MALLOC_DEFINE(M_DIRFS_MISC, "dirfs misc", "dirfs miscellaneous allocation");

/*
 * Kernel tracing facilities
 */
KTR_INFO_MASTER(dirfs);

KTR_INFO(KTR_DIRFS, dirfs, root, 20,
    "DIRFS(root dnp=%p vnode=%p hostdir=%s fd=%d error=%d)",
    dirfs_node_t dnp, struct vnode *vp, char *hostdir, int fd, int error);

KTR_INFO(KTR_DIRFS, dirfs, mount, 21,
    "DIRFS(mount path=%s dmp=%p mp=%p error=%d)",
    char *path, dirfs_mount_t dmp, struct mount *mp, int error);

KTR_INFO(KTR_DIRFS, dirfs, unmount, 22,
    "DIRFS(unmount dmp=%p mp=%p error=%d)",
    dirfs_mount_t dmp, struct mount *mp, int error);

/* System wide sysctl stuff */
int debuglvl = 0;
int dirfs_fd_limit = 100;
int dirfs_fd_used = 0;
long passive_fd_list_miss = 0;
long passive_fd_list_hits = 0;

SYSCTL_NODE(_vfs, OID_AUTO, dirfs, CTLFLAG_RW, 0,
    "dirfs filesystem for vkernels");
SYSCTL_INT(_vfs_dirfs, OID_AUTO, debug, CTLFLAG_RW,
    &debuglvl, 0, "dirfs debug level");
SYSCTL_INT(_vfs_dirfs, OID_AUTO, fd_limit, CTLFLAG_RW,
    &dirfs_fd_limit, 0, "Maximum number of passive nodes to cache");
SYSCTL_INT(_vfs_dirfs, OID_AUTO, fd_used, CTLFLAG_RD,
    &dirfs_fd_used, 0, "Current number of passive nodes cached");
SYSCTL_LONG(_vfs_dirfs, OID_AUTO, passive_fd_list_miss, CTLFLAG_RD,
    &passive_fd_list_miss, 0, "Passive fd list cache misses");
SYSCTL_LONG(_vfs_dirfs, OID_AUTO, passive_fd_list_hits, CTLFLAG_RD,
    &passive_fd_list_hits, 0, "Passive fd list cache misses");

static int dirfs_statfs(struct mount *, struct statfs *, struct ucred *);

static int
dirfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	dirfs_mount_t dmp;
	struct stat st;
	size_t done, nlen;
	int error;

	debug_called();

	if (mp->mnt_flag & MNT_UPDATE) {
		dmp = VFS_TO_DIRFS(mp);
		if (dmp->dm_rdonly == 0 && (mp->mnt_flag & MNT_RDONLY)) {
			/* XXX We should make sure all writes are synced */
			dmp->dm_rdonly = 1;
			debug(2, "dirfs read-write -> read-only\n");
		}

		if (dmp->dm_rdonly && (mp->mnt_kern_flag & MNTK_WANTRDWR)) {
			debug(2, "dirfs read-only -> read-write\n");
			dmp->dm_rdonly = 0;
		}
		return 0;
	}

	dmp = kmalloc(sizeof(*dmp), M_DIRFS, M_WAITOK | M_ZERO);
	mp->mnt_data = (qaddr_t)dmp;
	dmp->dm_mount = mp;

	error = copyinstr(data, &dmp->dm_path, MAXPATHLEN, &done);
	if (error) {
		/* Attempt to copy from kernel address */
		error = copystr(data, &dmp->dm_path, MAXPATHLEN, &done);
		if (error) {
			kfree(dmp, M_DIRFS);
			goto failure;
		}
	}

	/* Strip / character at the end to avoid problems */
	nlen = strnlen(dmp->dm_path, MAXPATHLEN);
	if (dmp->dm_path[nlen-1] == '/')
		dmp->dm_path[nlen-1] = 0;

	/* Make sure host directory exists and it is indeed a directory. */
	if ((stat(dmp->dm_path, &st)) == 0) {
		if (!S_ISDIR(st.st_mode)) {
			kfree(dmp, M_DIRFS);
			error = EINVAL;
			goto failure;
		}
	} else {
		error = errno;
		goto failure;
	}

	lockinit(&dmp->dm_lock, "dfsmnt", 0, LK_CANRECURSE);

	vfs_add_vnodeops(mp, &dirfs_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_getnewfsid(mp);

	/* Who is running the vkernel */
	dmp->dm_uid = getuid();
	dmp->dm_gid = getgid();

	TAILQ_INIT(&dmp->dm_fdlist);
	RB_INIT(&dmp->dm_inotree);

	kmalloc_raise_limit(M_DIRFS_NODE, 0);

	dirfs_statfs(mp, &mp->mnt_stat, cred);

failure:
	KTR_LOG(dirfs_mount, (dmp->dm_path) ? dmp->dm_path : "NULL",
	    dmp, mp, error);

	return error;
}

static int
dirfs_unmount(struct mount *mp, int mntflags)
{
	dirfs_mount_t dmp;
	dirfs_node_t dnp;
	int cnt;
	int error;

	debug_called();
	cnt = 0;
	dmp = VFS_TO_DIRFS(mp);

	error = vflush(mp, 0, 0);
	if (error)
		goto failure;

	/*
	 * Clean up dm_fdlist.  There should be no vnodes left so the
	 * only ref should be from the fdlist.
	 */
	while ((dnp = TAILQ_FIRST(&dmp->dm_fdlist)) != NULL) {
		dirfs_node_setpassive(dmp, dnp, 0);
	}

	/*
	 * Cleanup root node. In the case the filesystem is mounted
	 * but no operation is done on it, there will be no call to
	 * VFS_ROOT() so better check dnp is not NULL before attempting
	 * to release it.
	 */
	dnp = dmp->dm_root;
	if (dnp != NULL) {
		dirfs_close_helper(dnp);
		debug_node2(dnp);
		dirfs_node_drop(dmp, dnp); /* last ref should free structure */
	}
	kfree(dmp, M_DIRFS);
	mp->mnt_data = (qaddr_t) 0;

failure:
	KTR_LOG(dirfs_unmount, dmp, mp, error);

	return error;
}

static int
dirfs_root(struct mount *mp, struct vnode **vpp)
{
	dirfs_mount_t dmp;
	dirfs_node_t dnp;
	int fd;
	int error;

	debug_called();

	dmp = VFS_TO_DIRFS(mp);
	KKASSERT(dmp != NULL);

	if (dmp->dm_root == NULL) {
		/*
		 * dm_root holds the root dirfs node. Allocate a new one since
		 * there is none. Also attempt to lstat(2) it, in order to set
		 * data for VOP_ACCESS()
		 */
		dnp = dirfs_node_alloc(mp);
		error = dirfs_node_stat(DIRFS_NOFD, dmp->dm_path, dnp);
		if (error != 0) {
			dirfs_node_free(dmp, dnp);
			return error;
		}
		dirfs_node_ref(dnp);	/* leave inact for life of mount */

		/* Root inode's parent is NULL, used for verification */
		dnp->dn_parent = NULL;
		dmp->dm_root = dnp;
		dirfs_node_setflags(dnp, DIRFS_ROOT);

		/*
		 * Maintain an open descriptor on the root dnp.  The
		 * normal open/close/cache does not apply for the root
		 * so the descriptor is ALWAYS available.
		 */
		fd = open(dmp->dm_path, O_DIRECTORY);
		if (fd == -1) {
			dbg(5, "failed to open ROOT node\n");
			dirfs_free_vp(dmp, dnp);
			dirfs_node_free(dmp, dnp);
			return errno;
		}
		dnp->dn_fd = fd;
		dnp->dn_type = VDIR;
	} else {
		dnp = dmp->dm_root;
	}

	/*
	 * Acquire the root vnode (dn_type already set above).  This
	 * call will handle any races and return a locked vnode.
	 */
	dirfs_alloc_vp(mp, vpp, LK_CANRECURSE, dnp);
	KTR_LOG(dirfs_root, dnp, *vpp, dmp->dm_path, dnp->dn_fd, error);

	return 0;
}

static int
dirfs_fhtovp(struct mount *mp, struct vnode *rootvp, struct fid *fhp, struct vnode **vpp)
{
	debug_called();

	return EOPNOTSUPP;
}

static int
dirfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	dirfs_mount_t dmp = VFS_TO_DIRFS(mp);
	struct statfs st;

	debug_called();

	if((statfs(dmp->dm_path, &st)) == -1)
		return errno;

	ksnprintf(st.f_mntfromname, MNAMELEN - 1, "dirfs@%s", dmp->dm_path);
	bcopy(&st, sbp, sizeof(st));
	strlcpy(sbp->f_fstypename, mp->mnt_vfc->vfc_name, MFSNAMELEN);
	dbg(5, "iosize = %zd\n", sbp->f_iosize);

	return 0;
}

static int
dirfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	dirfs_mount_t dmp = VFS_TO_DIRFS(mp);
	struct statvfs st;

	debug_called();

	if ((statvfs(dmp->dm_path, &st)) == -1)
		return errno;

	bcopy(&st, sbp, sizeof(st));

	return 0;
}

static int
dirfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	dirfs_node_t dnp;

	dnp = VP_TO_NODE(vp);
	debug_node2(dnp);
	debug_called();

	return EOPNOTSUPP;
}

static int
dirfs_checkexp(struct mount *mp, struct sockaddr *nam, int *exflagsp,
	       struct ucred **credanonp)
{
	debug_called();

	return EOPNOTSUPP;
}

static struct vfsops dirfs_vfsops = {
	.vfs_mount =			dirfs_mount,
	.vfs_unmount =			dirfs_unmount,
	.vfs_root =			dirfs_root,
	.vfs_vget =			vfs_stdvget,
	.vfs_statfs =			dirfs_statfs,
	.vfs_statvfs =			dirfs_statvfs,
	.vfs_fhtovp =			dirfs_fhtovp,
	.vfs_vptofh =			dirfs_vptofh,
	.vfs_sync =			vfs_stdsync,
	.vfs_checkexp =			dirfs_checkexp
};

VFS_SET(dirfs_vfsops, dirfs, 0);
MODULE_VERSION(dirfs, 1);
