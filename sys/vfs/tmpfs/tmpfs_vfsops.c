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
#include <vfs/tmpfs/tmpfs_args.h>

/*
 * Default permission for root node
 */
#define TMPFS_DEFAULT_ROOT_MODE	(S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)

MALLOC_DEFINE(M_TMPFSMNT, "tmpfs mount", "tmpfs mount structures");

/* --------------------------------------------------------------------- */

static int	tmpfs_mount(struct mount *, char *, caddr_t, struct ucred *);
static int	tmpfs_unmount(struct mount *, int);
static int	tmpfs_root(struct mount *, struct vnode **);
static int	tmpfs_fhtovp(struct mount *, struct vnode *, struct fid *, struct vnode **);
static int	tmpfs_statfs(struct mount *, struct statfs *, struct ucred *cred);

/* --------------------------------------------------------------------- */
int
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
	bzero(&node->tn_spec, sizeof(node->tn_spec));

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
	if (node == NULL)
		return (NULL);
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

static int
tmpfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	struct tmpfs_mount *tmp;
	struct tmpfs_node *root;
	struct tmpfs_args args;
	vm_pindex_t pages;
	vm_pindex_t pages_limit;
	ino_t nodes;
	u_int64_t	maxfsize;
	int error;
	/* Size counters. */
	ino_t	nodes_max;
	off_t	size_max;
	size_t	maxfsize_max;
	size_t	size;

	/* Root node attributes. */
	uid_t	root_uid = cred->cr_uid;
	gid_t	root_gid = cred->cr_gid;
	mode_t	root_mode = (VREAD | VWRITE);

	if (mp->mnt_flag & MNT_UPDATE) {
		/* XXX: There is no support yet to update file system
		 * settings.  Should be added. */

		return EOPNOTSUPP;
	}

	/*
	 * mount info
	 */
	bzero(&args, sizeof(args));
	size_max  = 0;
	nodes_max = 0;
	maxfsize_max = 0;

	if (path) {
		if (data) {
			error = copyin(data, &args, sizeof(args));
			if (error)
				return (error);
		}
		size_max = args.ta_size_max;
		nodes_max = args.ta_nodes_max;
		maxfsize_max = args.ta_maxfsize_max;
		root_uid = args.ta_root_uid;
		root_gid = args.ta_root_gid;
		root_mode = args.ta_root_mode;
	}

	/*
	 * If mount by non-root, then verify that user has necessary
	 * permissions on the device.
	 */
	if (cred->cr_uid != 0) {
		root_mode = VREAD;
		if ((mp->mnt_flag & MNT_RDONLY) == 0)
			root_mode |= VWRITE;
	}

	pages_limit = vm_swap_max + vmstats.v_page_count / 2;

	if (size_max == 0) {
		pages = pages_limit / 2;
	} else if (size_max < PAGE_SIZE) {
		pages = 1;
	} else if (OFF_TO_IDX(size_max) > pages_limit) {
		/*
		 * do not force pages = pages_limit for this case, otherwise
		 * we might not honor tmpfs size requests from /etc/fstab
		 * during boot because they are mounted prior to swap being
		 * turned on.
		 */
		pages = OFF_TO_IDX(size_max);
	} else {
		pages = OFF_TO_IDX(size_max);
	}

	if (nodes_max == 0)
		nodes = 3 + pages * PAGE_SIZE / 1024;
	else if (nodes_max < 3)
		nodes = 3;
	else if (nodes_max > pages)
		nodes = pages;
	else
		nodes = nodes_max;

	maxfsize = IDX_TO_OFF(pages_limit);
	if (maxfsize_max != 0 && maxfsize > maxfsize_max)
		maxfsize = maxfsize_max;

	/* Allocate the tmpfs mount structure and fill it. */
	tmp = kmalloc(sizeof(*tmp), M_TMPFSMNT, M_WAITOK | M_ZERO);

	lockinit(&(tmp->allnode_lock), "tmpfs allnode lock", 0, LK_CANRECURSE);
	tmp->tm_nodes_max = nodes;
	tmp->tm_nodes_inuse = 0;
	tmp->tm_maxfilesize = maxfsize;
	LIST_INIT(&tmp->tm_nodes_used);

	tmp->tm_pages_max = pages;
	tmp->tm_pages_used = 0;

	kmalloc_create(&tmp->tm_node_zone, "tmpfs node");
	kmalloc_create(&tmp->tm_dirent_zone, "tmpfs dirent");
	kmalloc_create(&tmp->tm_name_zone, "tmpfs name zone");

	kmalloc_raise_limit(tmp->tm_node_zone, sizeof(struct tmpfs_node) *
			    tmp->tm_nodes_max);

	tmp->tm_node_zone_malloc_args.objsize = sizeof(struct tmpfs_node);
	tmp->tm_node_zone_malloc_args.mtype = tmp->tm_node_zone;

	tmp->tm_dirent_zone_malloc_args.objsize = sizeof(struct tmpfs_dirent);
	tmp->tm_dirent_zone_malloc_args.mtype = tmp->tm_dirent_zone;

	tmp->tm_dirent_pool =  objcache_create( "tmpfs dirent cache",
	    0, 0,
	    NULL, NULL, NULL,
	    objcache_malloc_alloc, objcache_malloc_free,
	    &tmp->tm_dirent_zone_malloc_args);
	tmp->tm_node_pool = objcache_create( "tmpfs node cache",
	    0, 0,
	    tmpfs_node_ctor, tmpfs_node_dtor, NULL,
	    tmpfs_node_init, tmpfs_node_fini,
	    &tmp->tm_node_zone_malloc_args);

	tmp->tm_ino = 2;

	/* Allocate the root node. */
	error = tmpfs_alloc_node(tmp, VDIR, root_uid, root_gid,
				 root_mode & ALLPERMS, NULL, NULL,
				 VNOVAL, VNOVAL, &root);

	/*
	 * We are backed by swap, set snocache chflags flag so we
	 * don't trip over swapcache.
	 */
	root->tn_flags = SF_NOCACHE;

	if (error != 0 || root == NULL) {
	    objcache_destroy(tmp->tm_node_pool);
	    objcache_destroy(tmp->tm_dirent_pool);
	    kfree(tmp, M_TMPFSMNT);
	    return error;
	}
	KASSERT(root->tn_id >= 0, ("tmpfs root with invalid ino: %d", (int)root->tn_id));
	tmp->tm_root = root;

	mp->mnt_flag |= MNT_LOCAL;
#if 0
	mp->mnt_kern_flag |= MNTK_RD_MPSAFE | MNTK_WR_MPSAFE | MNTK_GA_MPSAFE  |
			     MNTK_IN_MPSAFE | MNTK_SG_MPSAFE;
#endif
	mp->mnt_kern_flag |= MNTK_RD_MPSAFE | MNTK_GA_MPSAFE | MNTK_SG_MPSAFE;
	mp->mnt_kern_flag |= MNTK_WR_MPSAFE;
	mp->mnt_kern_flag |= MNTK_NOMSYNC;
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
	int found;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *node;

	/* Handle forced unmounts. */
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	tmp = VFS_TO_TMPFS(mp);

	/*
	 * Finalize all pending I/O.  In the case of tmpfs we want
	 * to throw all the data away so clean out the buffer cache
	 * and vm objects before calling vflush().
	 */
	LIST_FOREACH(node, &tmp->tm_nodes_used, tn_entries) {
		if (node->tn_type == VREG && node->tn_vnode) {
			++node->tn_links;
			TMPFS_NODE_LOCK(node);
			vx_get(node->tn_vnode);
			tmpfs_truncate(node->tn_vnode, 0);
			vx_put(node->tn_vnode);
			TMPFS_NODE_UNLOCK(node);
			--node->tn_links;
		}
	}
	error = vflush(mp, 0, flags);
	if (error != 0)
		return error;

	/*
	 * First pass get rid of all the directory entries and
	 * vnode associations.  The directory structure will
	 * remain via the extra link count representing tn_dir.tn_parent.
	 *
	 * No vnodes should remain after the vflush above.
	 */
	LIST_FOREACH(node, &tmp->tm_nodes_used, tn_entries) {
		++node->tn_links;
		TMPFS_NODE_LOCK(node);
		if (node->tn_type == VDIR) {
			struct tmpfs_dirent *de;

			while (!RB_EMPTY(&node->tn_dir.tn_dirtree)) {
				de = RB_FIRST(tmpfs_dirtree, &node->tn_dir.tn_dirtree);
				tmpfs_dir_detach(node, de);
				tmpfs_free_dirent(tmp, de);
				node->tn_size -= sizeof(struct tmpfs_dirent);
			}
		}
		KKASSERT(node->tn_vnode == NULL);
#if 0
		vp = node->tn_vnode;
		if (vp != NULL) {
			tmpfs_free_vp(vp);
			vrecycle(vp);
			node->tn_vnode = NULL;
		}
#endif
		TMPFS_NODE_UNLOCK(node);
		--node->tn_links;
	}

	/*
	 * Now get rid of all nodes.  We can remove any node with a
	 * link count of 0 or any directory node with a link count of
	 * 1.  The parents will not be destroyed until all their children
	 * have been destroyed.
	 *
	 * Recursion in tmpfs_free_node() can further modify the list so
	 * we cannot use a next pointer here.
	 *
	 * The root node will be destroyed by this loop (it will be last).
	 */
	while (!LIST_EMPTY(&tmp->tm_nodes_used)) {
		found = 0;
		LIST_FOREACH(node, &tmp->tm_nodes_used, tn_entries) {
			if (node->tn_links == 0 ||
			    (node->tn_links == 1 && node->tn_type == VDIR)) {
				TMPFS_NODE_LOCK(node);
				tmpfs_free_node(tmp, node);
				/* eats lock */
				found = 1;
				break;
			}
		}
		if (found == 0) {
			kprintf("tmpfs: Cannot free entire node tree!");
			break;
		}
	}

	KKASSERT(tmp->tm_root == NULL);

	objcache_destroy(tmp->tm_dirent_pool);
	objcache_destroy(tmp->tm_node_pool);

	kmalloc_destroy(&tmp->tm_name_zone);
	kmalloc_destroy(&tmp->tm_dirent_zone);
	kmalloc_destroy(&tmp->tm_node_zone);

	tmp->tm_node_zone = tmp->tm_dirent_zone = NULL;

	lockuninit(&tmp->allnode_lock);
	KKASSERT(tmp->tm_pages_used == 0);
	KKASSERT(tmp->tm_nodes_inuse == 0);

	/* Throw away the tmpfs_mount structure. */
	kfree(tmp, M_TMPFSMNT);
	mp->mnt_data = NULL;

	mp->mnt_flag &= ~MNT_LOCAL;
	return 0;
}

/* --------------------------------------------------------------------- */

static int
tmpfs_root(struct mount *mp, struct vnode **vpp)
{
	struct tmpfs_mount *tmp;
	int error;

	tmp = VFS_TO_TMPFS(mp);
	if (tmp->tm_root == NULL) {
		kprintf("tmpfs_root: called without root node %p\n", mp);
		print_backtrace(-1);
		*vpp = NULL;
		error = EINVAL;
	} else {
		error = tmpfs_alloc_vp(mp, tmp->tm_root, LK_EXCLUSIVE, vpp);
		(*vpp)->v_flag |= VROOT;
		(*vpp)->v_type = VDIR;
	}
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

	sbp->f_blocks = tmp->tm_pages_max;
	sbp->f_bavail = tmp->tm_pages_max - tmp->tm_pages_used;
	sbp->f_bfree = sbp->f_bavail;

	freenodes = tmp->tm_nodes_max - tmp->tm_nodes_inuse;

	sbp->f_files = freenodes + tmp->tm_nodes_inuse;
	sbp->f_ffree = freenodes;
	sbp->f_owner = tmp->tm_root->tn_uid;

	return 0;
}

/* --------------------------------------------------------------------- */ 

static int
tmpfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	struct tmpfs_node *node;
	struct tmpfs_fid tfh;
	node = VP_TO_TMPFS_NODE(vp);
	memset(&tfh, 0, sizeof(tfh));
	tfh.tf_len = sizeof(struct tmpfs_fid);
	tfh.tf_gen = node->tn_gen;
	tfh.tf_id = node->tn_id;
	memcpy(fhp, &tfh, sizeof(tfh));
	return (0);
}

/* --------------------------------------------------------------------- */

static int 
tmpfs_checkexp(struct mount *mp, struct sockaddr *nam, int *exflagsp, 
	       struct ucred **credanonp) 
{ 
	struct tmpfs_mount *tmp; 
	struct netcred *nc; 
 
	tmp = (struct tmpfs_mount *) mp->mnt_data;
	nc = vfs_export_lookup(mp, &tmp->tm_export, nam); 
	if (nc == NULL) 
		return (EACCES); 

	*exflagsp = nc->netc_exflags; 
	*credanonp = &nc->netc_anon; 
 
	return (0); 
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
	.vfs_vptofh =			tmpfs_vptofh, 
	.vfs_sync =			vfs_stdsync,
	.vfs_checkexp =			tmpfs_checkexp,
};

VFS_SET(tmpfs_vfsops, tmpfs, 0);
MODULE_VERSION(tmpfs, 1);
