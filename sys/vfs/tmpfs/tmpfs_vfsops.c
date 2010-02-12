/*	$NetBSD: tmpfs_vfsops.c,v 1.10 2005/12/11 12:24:29 christos Exp $	*/

/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Efficient memory file system.
 *
 * tmpfs is a file system that uses NetBSD's virtual memory sub-system
 * (the well-known UVM) to store file data and metadata in an efficient
 * way.  This means that it does not follow the structure of an on-disk
 * file system because it simply does not need to.  Instead, it uses
 * memory-specific data structures and algorithms to automatically
 * allocate and release resources.
 */
#include <sys/cdefs.h>
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/objcache.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_param.h>

#include <vfs/tmpfs/tmpfs.h>
#include <vfs/tmpfs/tmpfs_vnops.h>

/*
 * Default permission for root node
 */
#define TMPFS_DEFAULT_ROOT_MODE	(S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)

MALLOC_DEFINE(M_TMPFSMNT, "tmpfs mount", "tmpfs mount structures");
MALLOC_DEFINE(M_TMPFSNAME, "tmpfs name", "tmpfs file names");
MALLOC_DEFINE(M_TMPFS_DIRENT, "tmpfs dirent", "tmpfs dirent structures");
MALLOC_DEFINE(M_TMPFS_NODE, "tmpfs node", "tmpfs node structures");

/* --------------------------------------------------------------------- */

static int	tmpfs_mount(struct mount *, char *, caddr_t, struct ucred *);
static int	tmpfs_unmount(struct mount *, int);
static int	tmpfs_root(struct mount *, struct vnode **);
static int	tmpfs_fhtovp(struct mount *, struct vnode *, struct fid *, struct vnode **);
static int	tmpfs_statfs(struct mount *, struct statfs *, struct ucred *cred);

/* --------------------------------------------------------------------- */

#define SWI_MAXMIB	3
static u_int
get_swpgtotal(void)
{
	struct swdevt swinfo;
	char *sname = "vm.swap_info";
	int soid[SWI_MAXMIB], oid[2];
	u_int unswdev, total, dmmax, nswapdev;
	size_t mibi, len;

	total = 0;

	len = sizeof(dmmax);
	if (kernel_sysctlbyname("vm.dmmax", &dmmax, &len,
				NULL, 0, NULL) != 0)
		return total;

	len = sizeof(nswapdev);
	if (kernel_sysctlbyname("vm.nswapdev", &nswapdev, &len,
				NULL, 0, NULL) != 0)
		return total;

	mibi = (SWI_MAXMIB - 1) * sizeof(int);
	oid[0] = 0;
	oid[1] = 3;

	if (kernel_sysctl(oid, 2,
			soid, &mibi, (void *)sname, strlen(sname),
			NULL) != 0)
		return total;

	mibi = (SWI_MAXMIB - 1);
	for (unswdev = 0; unswdev < nswapdev; ++unswdev) {
		soid[mibi] = unswdev;
		len = sizeof(struct swdevt);
		if (kernel_sysctl(soid, mibi + 1, &swinfo, &len, NULL, 0,
				NULL) != 0)
			return total;
		if (len == sizeof(struct swdevt))
			total += (swinfo.sw_nblks - dmmax);
	}

	return total;
}

/* --------------------------------------------------------------------- */
static int
tmpfs_node_ctor(void *obj, void *privdata, int flags)
{
	struct tmpfs_node *node = (struct tmpfs_node *)obj;

	node->tn_gen++;
	node->tn_size = 0;
	node->tn_status = 0;
	node->tn_flags = 0;
	node->tn_links = 0;
	node->tn_vnode = NULL;
	node->tn_vpstate = TMPFS_VNODE_WANT;

	return (1);
}

static void
tmpfs_node_dtor(void *obj, void *privdata)
{
	struct tmpfs_node *node = (struct tmpfs_node *)obj;
	node->tn_type = VNON;
	node->tn_vpstate = TMPFS_VNODE_DOOMED;
}

static void*
tmpfs_node_init(void *args, int flags)
{
	struct tmpfs_node *node = (struct tmpfs_node *)objcache_malloc_alloc(args, flags);
	node->tn_id = 0;

	lockinit(&node->tn_interlock, "tmpfs node interlock", 0, LK_CANRECURSE);
	node->tn_gen = karc4random();

	return node;
}

static void
tmpfs_node_fini(void *obj, void *args)
{
	struct tmpfs_node *node = (struct tmpfs_node *)obj;
	lockuninit(&node->tn_interlock);
	objcache_malloc_free(obj, args);
}

struct objcache_malloc_args tmpfs_dirent_pool_malloc_args =
	{ sizeof(struct tmpfs_dirent), M_TMPFS_DIRENT };
struct objcache_malloc_args tmpfs_node_pool_malloc_args =
	{ sizeof(struct tmpfs_node), M_TMPFS_NODE };

static int
tmpfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	struct tmpfs_mount *tmp;
	struct tmpfs_node *root;
	size_t pages, mem_size;
	ino_t nodes;
	int error;
	/* Size counters. */
	ino_t	nodes_max = 0;
	size_t	size_max = 0;
	size_t size;

	/* Root node attributes. */
	uid_t	root_uid = cred->cr_uid;
	gid_t	root_gid = cred->cr_gid;
	mode_t	root_mode = (VREAD | VWRITE);

	if (mp->mnt_flag & MNT_UPDATE) {
		/* XXX: There is no support yet to update file system
		 * settings.  Should be added. */

		return EOPNOTSUPP;
	}

	kprintf("WARNING: TMPFS is considered to be a highly experimental "
	    "feature in DragonFly.\n");

	/* Do not allow mounts if we do not have enough memory to preserve
	 * the minimum reserved pages. */
	mem_size = vmstats.v_free_count + vmstats.v_inactive_count + get_swpgtotal();
	mem_size -= mem_size > vmstats.v_wire_count ? vmstats.v_wire_count : mem_size;
	if (mem_size < TMPFS_PAGES_RESERVED)
		return ENOSPC;

	/*
	 * If mount by non-root, then verify that user has necessary
	 * permissions on the device.
	 */
	if (cred->cr_uid != 0) {
		root_mode = VREAD;
		if ((mp->mnt_flag & MNT_RDONLY) == 0)
			root_mode |= VWRITE;
	}

	/* Get the maximum number of memory pages this file system is
	 * allowed to use, based on the maximum size the user passed in
	 * the mount structure.  A value of zero is treated as if the
	 * maximum available space was requested. */
	if (size_max < PAGE_SIZE || size_max >= SIZE_MAX)
		pages = SIZE_MAX;
	else
		pages = howmany(size_max, PAGE_SIZE);
	KKASSERT(pages > 0);

	if (nodes_max <= 3)
		nodes = 3 + pages * PAGE_SIZE / 1024;
	else
		nodes = nodes_max;
	KKASSERT(nodes >= 3);

	/* Allocate the tmpfs mount structure and fill it. */
	tmp = (struct tmpfs_mount *)kmalloc(sizeof(struct tmpfs_mount),
	    M_TMPFSMNT, M_WAITOK | M_ZERO);

	lockinit(&(tmp->allnode_lock), "tmpfs allnode lock", 0, LK_CANRECURSE);
	tmp->tm_nodes_max = nodes;
	tmp->tm_nodes_inuse = 0;
	tmp->tm_maxfilesize = (u_int64_t)(vmstats.v_page_count + get_swpgtotal()) * PAGE_SIZE;
	LIST_INIT(&tmp->tm_nodes_used);

	tmp->tm_pages_max = pages;
	tmp->tm_pages_used = 0;
	tmp->tm_dirent_pool =  objcache_create( "tmpfs dirent cache",
	    0, 0,
	    NULL, NULL, NULL,
	    objcache_malloc_alloc, objcache_malloc_free,
	    &tmpfs_dirent_pool_malloc_args);
	tmp->tm_node_pool = objcache_create( "tmpfs node cache",
	    0, 0,
	    tmpfs_node_ctor, tmpfs_node_dtor, NULL,
	    tmpfs_node_init, tmpfs_node_fini,
	    &tmpfs_node_pool_malloc_args);

	/* Allocate the root node. */
	error = tmpfs_alloc_node(tmp, VDIR, root_uid,
	    root_gid, root_mode & ALLPERMS, NULL, NULL,
	    VNOVAL, VNOVAL, &root);

	if (error != 0 || root == NULL) {
	    objcache_destroy(tmp->tm_node_pool);
	    objcache_destroy(tmp->tm_dirent_pool);
	    kfree(tmp, M_TMPFSMNT);
	    return error;
	}
	KASSERT(root->tn_id >= 0, ("tmpfs root with invalid ino: %d", (int)root->tn_id));
	tmp->tm_root = root;

	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_RD_MPSAFE | MNTK_WR_MPSAFE | MNTK_GA_MPSAFE  |
			     MNTK_IN_MPSAFE | MNTK_SG_MPSAFE;
	mp->mnt_data = (qaddr_t)tmp;
	vfs_getnewfsid(mp);


	vfs_add_vnodeops(mp, &tmpfs_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &tmpfs_fifo_vops, &mp->mnt_vn_fifo_ops);

	copystr("tmpfs", mp->mnt_stat.f_mntfromname, MNAMELEN - 1, &size);
	bzero(mp->mnt_stat.f_mntfromname +size, MNAMELEN - size);
	bzero(mp->mnt_stat.f_mntonname, sizeof(mp->mnt_stat.f_mntonname));
	copyinstr(path, mp->mnt_stat.f_mntonname,
		  sizeof(mp->mnt_stat.f_mntonname) -1,
		  &size);

	tmpfs_statfs(mp, &mp->mnt_stat, cred);

	return 0;
}

/* --------------------------------------------------------------------- */

/* ARGSUSED2 */
static int
tmpfs_unmount(struct mount *mp, int mntflags)
{
	int error;
	int flags = 0;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *node;
	struct vnode *vp;

	/* Handle forced unmounts. */
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	/* Finalize all pending I/O. */
	error = vflush(mp, 0, flags);
	if (error != 0)
		return error;

	tmp = VFS_TO_TMPFS(mp);

	/* Free all associated data.  The loop iterates over the linked list
	 * we have containing all used nodes.  For each of them that is
	 * a directory, we free all its directory entries.  Note that after
	 * freeing a node, it will automatically go to the available list,
	 * so we will later have to iterate over it to release its items. */
	node = LIST_FIRST(&tmp->tm_nodes_used);
	while (node != NULL) {
		struct tmpfs_node *next;

		if (node->tn_type == VDIR) {
			struct tmpfs_dirent *de;

			de = TAILQ_FIRST(&node->tn_dir.tn_dirhead);
			while (de != NULL) {
				struct tmpfs_dirent *nde;

				nde = TAILQ_NEXT(de, td_entries);
				tmpfs_free_dirent(tmp, de, FALSE);
				de = nde;
				node->tn_size -= sizeof(struct tmpfs_dirent);
			}
		}

		next = LIST_NEXT(node, tn_entries);
		vp = node->tn_vnode;
		if (vp != NULL) {
			tmpfs_free_vp(vp);
			vrecycle(vp);
			 node->tn_vnode = NULL;
		}
		tmpfs_free_node(tmp, node);
		node = next;
	}

	objcache_destroy(tmp->tm_dirent_pool);
	objcache_destroy(tmp->tm_node_pool);

	lockuninit(&tmp->allnode_lock);
	KKASSERT(tmp->tm_pages_used == 0);
	KKASSERT(tmp->tm_nodes_inuse == 0);

	/* Throw away the tmpfs_mount structure. */
	kfree(mp->mnt_data, M_TMPFSMNT);
	mp->mnt_data = NULL;

	mp->mnt_flag &= ~MNT_LOCAL;
	return 0;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_root(struct mount *mp, struct vnode **vpp)
{
	int error;
	error = tmpfs_alloc_vp(mp, VFS_TO_TMPFS(mp)->tm_root, LK_EXCLUSIVE, vpp);
	(*vpp)->v_flag |= VROOT;
	(*vpp)->v_type = VDIR;

	return error;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_fhtovp(struct mount *mp, struct vnode *rootvp, struct fid *fhp, struct vnode **vpp)
{
	boolean_t found;
	struct tmpfs_fid *tfhp;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *node;

	tmp = VFS_TO_TMPFS(mp);

	tfhp = (struct tmpfs_fid *)fhp;
	if (tfhp->tf_len != sizeof(struct tmpfs_fid))
		return EINVAL;

	if (tfhp->tf_id >= tmp->tm_nodes_max)
		return EINVAL;

	found = FALSE;

	TMPFS_LOCK(tmp);
	LIST_FOREACH(node, &tmp->tm_nodes_used, tn_entries) {
		if (node->tn_id == tfhp->tf_id &&
		    node->tn_gen == tfhp->tf_gen) {
			found = TRUE;
			break;
		}
	}
	TMPFS_UNLOCK(tmp);

	if (found)
		return (tmpfs_alloc_vp(mp, node, LK_EXCLUSIVE, vpp));

	return (EINVAL);
}

/* --------------------------------------------------------------------- */

/* ARGSUSED2 */
static int
tmpfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	fsfilcnt_t freenodes;
	struct tmpfs_mount *tmp;

	tmp = VFS_TO_TMPFS(mp);

	sbp->f_iosize = PAGE_SIZE;
	sbp->f_bsize = PAGE_SIZE;

	sbp->f_blocks = TMPFS_PAGES_MAX(tmp);
	sbp->f_bavail = sbp->f_bfree = TMPFS_PAGES_AVAIL(tmp);

	freenodes = MIN(tmp->tm_nodes_max - tmp->tm_nodes_inuse,
	    TMPFS_PAGES_AVAIL(tmp) * PAGE_SIZE / sizeof(struct tmpfs_node));

	sbp->f_files = freenodes + tmp->tm_nodes_inuse;
	sbp->f_ffree = freenodes;
	/* sbp->f_owner = tmp->tn_uid; */

	return 0;
}

/* --------------------------------------------------------------------- */

/*
 * tmpfs vfs operations.
 */

static struct vfsops tmpfs_vfsops = {
	.vfs_mount =			tmpfs_mount,
	.vfs_unmount =			tmpfs_unmount,
	.vfs_root =			tmpfs_root,
	.vfs_statfs =			tmpfs_statfs,
	.vfs_fhtovp =			tmpfs_fhtovp,
	.vfs_sync =			vfs_stdsync
};

VFS_SET(tmpfs_vfsops, tmpfs, 0);
