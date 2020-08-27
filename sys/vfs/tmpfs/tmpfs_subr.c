/*	$NetBSD: tmpfs_subr.c,v 1.35 2007/07/09 21:10:50 ad Exp $	*/

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
 * Efficient memory file system supporting functions.
 */

#include <sys/kernel.h>
#include <sys/param.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_pageout.h>
#include <vm/vm_page2.h>

#include <vfs/tmpfs/tmpfs.h>
#include <vfs/tmpfs/tmpfs_vnops.h>

static ino_t tmpfs_fetch_ino(struct tmpfs_mount *);

static int tmpfs_dirtree_compare(struct tmpfs_dirent *a,
	struct tmpfs_dirent *b);
RB_GENERATE(tmpfs_dirtree, tmpfs_dirent, rb_node, tmpfs_dirtree_compare);

static int tmpfs_dirtree_compare_cookie(struct tmpfs_dirent *a,
	struct tmpfs_dirent *b);
RB_GENERATE(tmpfs_dirtree_cookie, tmpfs_dirent,
	rb_cookienode, tmpfs_dirtree_compare_cookie);


/* --------------------------------------------------------------------- */

/*
 * Allocates a new node of type 'type' inside the 'tmp' mount point, with
 * its owner set to 'uid', its group to 'gid' and its mode set to 'mode',
 * using the credentials of the process 'p'.
 *
 * If the node type is set to 'VDIR', then the parent parameter must point
 * to the parent directory of the node being created.  It may only be NULL
 * while allocating the root node.
 *
 * If the node type is set to 'VBLK' or 'VCHR', then the rdev parameter
 * specifies the device the node represents.
 *
 * If the node type is set to 'VLNK', then the parameter target specifies
 * the file name of the target file for the symbolic link that is being
 * created.
 *
 * Note that new nodes are retrieved from the available list if it has
 * items or, if it is empty, from the node pool as long as there is enough
 * space to create them.
 *
 * Returns zero on success or an appropriate error code on failure.
 */
int
tmpfs_alloc_node(struct tmpfs_mount *tmp, enum vtype type,
		 uid_t uid, gid_t gid, mode_t mode,
		 char *target, int rmajor, int rminor,
		 struct tmpfs_node **node)
{
	struct tmpfs_node *nnode;
	struct timespec ts;
	dev_t rdev;

	KKASSERT(IFF(type == VLNK, target != NULL));
	KKASSERT(IFF(type == VBLK || type == VCHR, rmajor != VNOVAL));

	if (tmp->tm_nodes_inuse >= tmp->tm_nodes_max)
		return (ENOSPC);

	nnode = objcache_get(tmp->tm_node_pool, M_WAITOK | M_NULLOK);
	if (nnode == NULL)
		return (ENOSPC);

	/* Generic initialization. */
	nnode->tn_type = type;
	vfs_timestamp(&ts);
	nnode->tn_ctime = nnode->tn_mtime = nnode->tn_atime
		= ts.tv_sec;
	nnode->tn_ctimensec = nnode->tn_mtimensec = nnode->tn_atimensec
		= ts.tv_nsec;
	nnode->tn_uid = uid;
	nnode->tn_gid = gid;
	nnode->tn_mode = mode;
	nnode->tn_id = tmpfs_fetch_ino(tmp);
	nnode->tn_advlock.init_done = 0;
	KKASSERT(nnode->tn_links == 0);

	/* Type-specific initialization. */
	switch (nnode->tn_type) {
	case VBLK:
	case VCHR:
		rdev = makeudev(rmajor, rminor);
		if (rdev == NOUDEV) {
			objcache_put(tmp->tm_node_pool, nnode);
			return(EINVAL);
		}
		nnode->tn_rdev = rdev;
		break;

	case VDIR:
		RB_INIT(&nnode->tn_dir.tn_dirtree);
		RB_INIT(&nnode->tn_dir.tn_cookietree);
		nnode->tn_dir.tn_parent = NULL;
		nnode->tn_size = 0;
		break;

	case VFIFO:
		/* FALLTHROUGH */
	case VSOCK:
		break;

	case VLNK:
		nnode->tn_size = strlen(target);
		nnode->tn_link = kmalloc(nnode->tn_size + 1, tmp->tm_name_zone,
					 M_WAITOK | M_NULLOK);
		if (nnode->tn_link == NULL) {
			objcache_put(tmp->tm_node_pool, nnode);
			return (ENOSPC);
		}
		bcopy(target, nnode->tn_link, nnode->tn_size);
		nnode->tn_link[nnode->tn_size] = '\0';
		break;

	case VREG:
		nnode->tn_reg.tn_aobj = swap_pager_alloc(NULL, 0,
							 VM_PROT_DEFAULT, 0);
		nnode->tn_reg.tn_aobj_pages = 0;
		nnode->tn_size = 0;
		vm_object_set_flag(nnode->tn_reg.tn_aobj, OBJ_NOPAGEIN);
		break;

	default:
		panic("tmpfs_alloc_node: type %p %d", nnode, (int)nnode->tn_type);
	}

	TMPFS_NODE_LOCK(nnode);
	TMPFS_LOCK(tmp);
	LIST_INSERT_HEAD(&tmp->tm_nodes_used, nnode, tn_entries);
	tmp->tm_nodes_inuse++;
	TMPFS_UNLOCK(tmp);
	TMPFS_NODE_UNLOCK(nnode);

	*node = nnode;
	return 0;
}

/* --------------------------------------------------------------------- */

/*
 * Destroys the node pointed to by node from the file system 'tmp'.
 * If the node does not belong to the given mount point, the results are
 * unpredicted.
 *
 * If the node references a directory; no entries are allowed because
 * their removal could need a recursive algorithm, something forbidden in
 * kernel space.  Furthermore, there is not need to provide such
 * functionality (recursive removal) because the only primitives offered
 * to the user are the removal of empty directories and the deletion of
 * individual files.
 *
 * Note that nodes are not really deleted; in fact, when a node has been
 * allocated, it cannot be deleted during the whole life of the file
 * system.  Instead, they are moved to the available list and remain there
 * until reused.
 *
 * A caller must have TMPFS_NODE_LOCK(node) and this function unlocks it.
 */
void
tmpfs_free_node(struct tmpfs_mount *tmp, struct tmpfs_node *node)
{
	vm_pindex_t pages = 0;

#ifdef INVARIANTS
	TMPFS_ASSERT_ELOCKED(node);
	KKASSERT(node->tn_vnode == NULL);
#endif

	TMPFS_LOCK(tmp);
	LIST_REMOVE(node, tn_entries);
	tmp->tm_nodes_inuse--;
	TMPFS_UNLOCK(tmp);
	TMPFS_NODE_UNLOCK(node);  /* Caller has this lock */

	switch (node->tn_type) {
	case VNON:
		/* Do not do anything.  VNON is provided to let the
		 * allocation routine clean itself easily by avoiding
		 * duplicating code in it. */
		/* FALLTHROUGH */
	case VBLK:
		/* FALLTHROUGH */
	case VCHR:
		/* FALLTHROUGH */
		break;
	case VDIR:
		/*
		 * The parent link can be NULL if this is the root
		 * node or if it is a directory node that was rmdir'd.
		 *
		 * XXX what if node is a directory which still contains
		 * directory entries (e.g. due to a forced umount) ?
		 */
		node->tn_size = 0;
		KKASSERT(node->tn_dir.tn_parent == NULL);

		/*
		 * If the root node is being destroyed don't leave a
		 * dangling pointer in tmpfs_mount.
		 */
		if (node == tmp->tm_root)
			tmp->tm_root = NULL;
		break;
	case VFIFO:
		/* FALLTHROUGH */
	case VSOCK:
		break;

	case VLNK:
		kfree(node->tn_link, tmp->tm_name_zone);
		node->tn_link = NULL;
		node->tn_size = 0;
		break;

	case VREG:
		if (node->tn_reg.tn_aobj != NULL)
			vm_object_deallocate(node->tn_reg.tn_aobj);
		node->tn_reg.tn_aobj = NULL;
		pages = node->tn_reg.tn_aobj_pages;
		break;

	default:
		panic("tmpfs_free_node: type %p %d", node, (int)node->tn_type);
	}

	/*
	 * Clean up fields for the next allocation.  The objcache only ctors
	 * new allocations.
	 */
	tmpfs_node_ctor(node, NULL, 0);
	objcache_put(tmp->tm_node_pool, node);
	/* node is now invalid */

	if (pages)
		atomic_add_long(&tmp->tm_pages_used, -(long)pages);
}

/* --------------------------------------------------------------------- */

/*
 * Allocates a new directory entry for the node node with a name of name.
 * The new directory entry is returned in *de.
 *
 * The link count of node is increased by one to reflect the new object
 * referencing it.
 *
 * Returns zero on success or an appropriate error code on failure.
 */
int
tmpfs_alloc_dirent(struct tmpfs_mount *tmp, struct tmpfs_node *node,
		   const char *name, uint16_t len, struct tmpfs_dirent **de)
{
	struct tmpfs_dirent *nde;

	nde = objcache_get(tmp->tm_dirent_pool, M_WAITOK);
	nde->td_name = kmalloc(len + 1, tmp->tm_name_zone, M_WAITOK | M_NULLOK);
	if (nde->td_name == NULL) {
		objcache_put(tmp->tm_dirent_pool, nde);
		*de = NULL;
		return (ENOSPC);
	}
	nde->td_namelen = len;
	bcopy(name, nde->td_name, len);
	nde->td_name[len] = '\0';

	nde->td_node = node;

	atomic_add_int(&node->tn_links, 1);

	*de = nde;

	return 0;
}

/* --------------------------------------------------------------------- */

/*
 * Frees a directory entry.  It is the caller's responsibility to destroy
 * the node referenced by it if needed.
 *
 * The link count of node is decreased by one to reflect the removal of an
 * object that referenced it.  This only happens if 'node_exists' is true;
 * otherwise the function will not access the node referred to by the
 * directory entry, as it may already have been released from the outside.
 */
void
tmpfs_free_dirent(struct tmpfs_mount *tmp, struct tmpfs_dirent *de)
{
	struct tmpfs_node *node;

	node = de->td_node;

	KKASSERT(node->tn_links > 0);
	atomic_add_int(&node->tn_links, -1);

	kfree(de->td_name, tmp->tm_name_zone);
	de->td_namelen = 0;
	de->td_name = NULL;
	de->td_node = NULL;
	objcache_put(tmp->tm_dirent_pool, de);
}

/* --------------------------------------------------------------------- */

/*
 * Allocates a new vnode for the node node or returns a new reference to
 * an existing one if the node had already a vnode referencing it.  The
 * resulting locked vnode is returned in *vpp.
 *
 * Returns zero on success or an appropriate error code on failure.
 *
 * The caller must ensure that node cannot go away (usually by holding
 * the related directory entry).
 *
 * If dnode is non-NULL this routine avoids deadlocking against it but
 * can return EAGAIN.  Caller must try again.  The dnode lock will cycle
 * in this case, it remains locked on return in all cases.  dnode must
 * be shared-locked.
 */
int
tmpfs_alloc_vp(struct mount *mp,
	       struct tmpfs_node *dnode, struct tmpfs_node *node, int lkflag,
	       struct vnode **vpp)
{
	int error = 0;
	struct vnode *vp;

loop:
	vp = NULL;
	if (node->tn_vnode == NULL) {
		error = getnewvnode(VT_TMPFS, mp, &vp,
				    VLKTIMEOUT, LK_CANRECURSE);
		if (error)
			goto out;
	}

	/*
	 * Interlocked extraction from node.  This can race many things.
	 * We have to get a soft reference on the vnode while we hold
	 * the node locked, then acquire it properly and check for races.
	 */
	TMPFS_NODE_LOCK(node);
	if (node->tn_vnode) {
		if (vp) {
			vp->v_type = VBAD;
			vx_put(vp);
		}
		vp = node->tn_vnode;

		KKASSERT((node->tn_vpstate & TMPFS_VNODE_DOOMED) == 0);
		vhold(vp);
		TMPFS_NODE_UNLOCK(node);

		if (dnode) {
			/*
			 * Special-case handling to avoid deadlocking against
			 * dnode.  This case has been validated and occurs
			 * every so often during synth builds.
			 */
			if (vget(vp, (lkflag & ~LK_RETRY) |
				     LK_NOWAIT |
				     LK_EXCLUSIVE) != 0) {
				TMPFS_NODE_UNLOCK(dnode);
				if (vget(vp, (lkflag & ~LK_RETRY) |
					     LK_SLEEPFAIL |
					     LK_EXCLUSIVE) == 0) {
					vn_unlock(vp);
				}
				vdrop(vp);
				TMPFS_NODE_LOCK_SH(dnode);

				return EAGAIN;
			}
		} else {
			/*
			 * Normal path
			 */
			if (vget(vp, lkflag | LK_EXCLUSIVE) != 0) {
				vdrop(vp);
				goto loop;
			}
		}
		if (node->tn_vnode != vp) {
			vput(vp);
			vdrop(vp);
			goto loop;
		}
		vdrop(vp);
		goto out;
	}

	/*
	 * We need to assign node->tn_vnode.  If vp is NULL, loop up to
	 * allocate the vp.  This can happen due to SMP races.
	 */
	if (vp == NULL) {
		TMPFS_NODE_UNLOCK(node);
		goto loop;
	}

	/*
	 * This should never happen.
	 */
	if (node->tn_vpstate & TMPFS_VNODE_DOOMED) {
		TMPFS_NODE_UNLOCK(node);
		vp->v_type = VBAD;
		vx_put(vp);
		error = ENOENT;
		goto out;
	}

	KKASSERT(node->tn_vnode == NULL);
	KKASSERT(vp != NULL);
	vp->v_data = node;
	vp->v_type = node->tn_type;

	/* Type-specific initialization. */
	switch (node->tn_type) {
	case VBLK:
		/* FALLTHROUGH */
	case VCHR:
		/* FALLTHROUGH */
	case VSOCK:
		break;
	case VREG:
		/*
		 * VMIO is mandatory.  Tmpfs also supports KVABIO
		 * for its tmpfs_strategy().
		 */
		vsetflags(vp, VKVABIO);
		vinitvmio(vp, node->tn_size, node->tn_blksize, -1);
		break;
	case VLNK:
		break;
	case VFIFO:
		vp->v_ops = &mp->mnt_vn_fifo_ops;
		break;
	case VDIR:
		break;

	default:
		panic("tmpfs_alloc_vp: type %p %d", node, (int)node->tn_type);
	}

	node->tn_vnode = vp;
	TMPFS_NODE_UNLOCK(node);

	vx_downgrade(vp);
out:
	*vpp = vp;
	KKASSERT(IFF(error == 0, *vpp != NULL && vn_islocked(*vpp)));

	return error;
}

/* --------------------------------------------------------------------- */

/*
 * Allocates a new file of type 'type' and adds it to the parent directory
 * 'dvp'; this addition is done using the component name given in 'cnp'.
 * The ownership of the new file is automatically assigned based on the
 * credentials of the caller (through 'cnp'), the group is set based on
 * the parent directory and the mode is determined from the 'vap' argument.
 * If successful, *vpp holds a vnode to the newly created file and zero
 * is returned.  Otherwise *vpp is NULL and the function returns an
 * appropriate error code.
 */
int
tmpfs_alloc_file(struct vnode *dvp, struct vnode **vpp, struct vattr *vap,
		 struct namecache *ncp, struct ucred *cred, char *target)
{
	int error;
	struct tmpfs_dirent *de;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *dnode;
	struct tmpfs_node *node;

	tmp = VFS_TO_TMPFS(dvp->v_mount);
	dnode = VP_TO_TMPFS_DIR(dvp);
	*vpp = NULL;

	TMPFS_NODE_LOCK(dnode);

	/*
	 * If the directory was removed but a process was CD'd into it,
	 * we do not allow any more file/dir creation within it.  Otherwise
	 * we will lose track of it.
	 */
	KKASSERT(dnode->tn_type == VDIR);
	if (dnode != tmp->tm_root && dnode->tn_dir.tn_parent == NULL) {
		TMPFS_NODE_UNLOCK(dnode);
		return ENOENT;
	}

	/*
	 * Make sure the link count does not overflow.
	 */
	if (vap->va_type == VDIR && dnode->tn_links >= LINK_MAX) {
		TMPFS_NODE_UNLOCK(dnode);
		return EMLINK;
	}

	/* Allocate a node that represents the new file. */
	error = tmpfs_alloc_node(tmp, vap->va_type, cred->cr_uid,
				 dnode->tn_gid, vap->va_mode, target,
				 vap->va_rmajor, vap->va_rminor, &node);
	if (error != 0) {
		TMPFS_NODE_UNLOCK(dnode);
		return error;
	}
	TMPFS_NODE_LOCK(node);

	/* Allocate a directory entry that points to the new file. */
	error = tmpfs_alloc_dirent(tmp, node, ncp->nc_name, ncp->nc_nlen, &de);
	if (error != 0) {
		TMPFS_NODE_UNLOCK(dnode);
		tmpfs_free_node(tmp, node);
		/* eats node lock */
		return error;
	}

	/* Allocate a vnode for the new file. */
	error = tmpfs_alloc_vp(dvp->v_mount, NULL, node, LK_EXCLUSIVE, vpp);
	if (error != 0) {
		TMPFS_NODE_UNLOCK(dnode);
		tmpfs_free_dirent(tmp, de);
		tmpfs_free_node(tmp, node);
		/* eats node lock */
		return error;
	}

	/*
	 * Now that all required items are allocated, we can proceed to
	 * insert the new node into the directory, an operation that
	 * cannot fail.
	 */
	tmpfs_dir_attach_locked(dnode, de);
	TMPFS_NODE_UNLOCK(dnode);
	TMPFS_NODE_UNLOCK(node);

	return error;
}

/* --------------------------------------------------------------------- */

/*
 * Attaches the directory entry de to the directory represented by dnode.
 * Note that this does not change the link count of the node pointed by
 * the directory entry, as this is done by tmpfs_alloc_dirent.
 *
 * dnode must be locked.
 */
void
tmpfs_dir_attach_locked(struct tmpfs_node *dnode, struct tmpfs_dirent *de)
{
	struct tmpfs_node *node = de->td_node;
	struct tmpfs_dirent *de2;

	if (node && node->tn_type == VDIR) {
		TMPFS_NODE_LOCK(node);
		atomic_add_int(&node->tn_links, 1);
		node->tn_status |= TMPFS_NODE_CHANGED;
		node->tn_dir.tn_parent = dnode;
		atomic_add_int(&dnode->tn_links, 1);
		TMPFS_NODE_UNLOCK(node);
	}
	de2 = RB_INSERT(tmpfs_dirtree, &dnode->tn_dir.tn_dirtree, de);
	KASSERT(de2 == NULL,
		("tmpfs_dir_attach_lockedA: duplicate insertion of %p, has %p\n",
		de, de2));
	de2 = RB_INSERT(tmpfs_dirtree_cookie, &dnode->tn_dir.tn_cookietree, de);
	KASSERT(de2 == NULL,
		("tmpfs_dir_attach_lockedB: duplicate insertion of %p, has %p\n",
		de, de2));
	dnode->tn_size += sizeof(struct tmpfs_dirent);
	dnode->tn_status |= TMPFS_NODE_ACCESSED | TMPFS_NODE_CHANGED |
			    TMPFS_NODE_MODIFIED;
}

/* --------------------------------------------------------------------- */

/*
 * Detaches the directory entry de from the directory represented by dnode.
 * Note that this does not change the link count of the node pointed by
 * the directory entry, as this is done by tmpfs_free_dirent.
 *
 * dnode must be locked.
 */
void
tmpfs_dir_detach_locked(struct tmpfs_node *dnode, struct tmpfs_dirent *de)
{
	struct tmpfs_node *node = de->td_node;

	RB_REMOVE(tmpfs_dirtree, &dnode->tn_dir.tn_dirtree, de);
	RB_REMOVE(tmpfs_dirtree_cookie, &dnode->tn_dir.tn_cookietree, de);
	dnode->tn_size -= sizeof(struct tmpfs_dirent);
	dnode->tn_status |= TMPFS_NODE_ACCESSED | TMPFS_NODE_CHANGED |
			    TMPFS_NODE_MODIFIED;

	/*
	 * Clean out the tn_parent pointer immediately when removing a
	 * directory.
	 *
	 * Removal of the parent linkage also cleans out the extra tn_links
	 * count we had on both node and dnode.
	 *
	 * node can be NULL (typ during a forced umount), in which case
	 * the mount code is dealing with the linkages from a linked list
	 * scan.
	 */
	if (node && node->tn_type == VDIR && node->tn_dir.tn_parent) {
		TMPFS_NODE_LOCK(node);
		KKASSERT(node->tn_dir.tn_parent == dnode);
		atomic_add_int(&dnode->tn_links, -1);
		atomic_add_int(&node->tn_links, -1);
		node->tn_dir.tn_parent = NULL;
		TMPFS_NODE_UNLOCK(node);
	}
}

/* --------------------------------------------------------------------- */

/*
 * Looks for a directory entry in the directory represented by node.
 * 'ncp' describes the name of the entry to look for.  Note that the .
 * and .. components are not allowed as they do not physically exist
 * within directories.
 *
 * Returns a pointer to the entry when found, otherwise NULL.
 *
 * Caller must hold the node locked (shared ok)
 */
struct tmpfs_dirent *
tmpfs_dir_lookup(struct tmpfs_node *node, struct tmpfs_node *f,
		 struct namecache *ncp)
{
	struct tmpfs_dirent *de;
	int len = ncp->nc_nlen;
	struct tmpfs_dirent wanted;

	wanted.td_namelen = len;
	wanted.td_name = ncp->nc_name;

	TMPFS_VALIDATE_DIR(node);

	de = RB_FIND(tmpfs_dirtree, &node->tn_dir.tn_dirtree, &wanted);

	KASSERT((f == NULL || de == NULL || f == de->td_node),
		("tmpfs_dir_lookup: Incorrect node %p %p %p",
		 f, de, (de ? de->td_node : NULL)));

	return de;
}

/* --------------------------------------------------------------------- */

/*
 * Helper function for tmpfs_readdir.  Creates a '.' entry for the given
 * directory and returns it in the uio space.  The function returns 0
 * on success, -1 if there was not enough space in the uio structure to
 * hold the directory entry or an appropriate error code if another
 * error happens.
 */
int
tmpfs_dir_getdotdent(struct tmpfs_node *node, struct uio *uio)
{
	int error;

	TMPFS_VALIDATE_DIR(node);
	KKASSERT(uio->uio_offset == TMPFS_DIRCOOKIE_DOT);

	if (vop_write_dirent(&error, uio, node->tn_id, DT_DIR, 1, "."))
		return -1;
	if (error == 0)
		uio->uio_offset = TMPFS_DIRCOOKIE_DOTDOT;
	return error;
}

/* --------------------------------------------------------------------- */

/*
 * Helper function for tmpfs_readdir.  Creates a '..' entry for the given
 * directory and returns it in the uio space.  The function returns 0
 * on success, -1 if there was not enough space in the uio structure to
 * hold the directory entry or an appropriate error code if another
 * error happens.
 */
int
tmpfs_dir_getdotdotdent(struct tmpfs_mount *tmp, struct tmpfs_node *node,
			struct uio *uio)
{
	int error;
	ino_t d_ino;

	TMPFS_VALIDATE_DIR(node);
	KKASSERT(uio->uio_offset == TMPFS_DIRCOOKIE_DOTDOT);

	if (node->tn_dir.tn_parent) {
		TMPFS_NODE_LOCK(node);
		if (node->tn_dir.tn_parent)
			d_ino = node->tn_dir.tn_parent->tn_id;
		else
			d_ino = tmp->tm_root->tn_id;
		TMPFS_NODE_UNLOCK(node);
	} else {
		d_ino = tmp->tm_root->tn_id;
	}

	if (vop_write_dirent(&error, uio, d_ino, DT_DIR, 2, ".."))
		return -1;
	if (error == 0) {
		struct tmpfs_dirent *de;
		de = RB_MIN(tmpfs_dirtree_cookie, &node->tn_dir.tn_cookietree);
		if (de == NULL)
			uio->uio_offset = TMPFS_DIRCOOKIE_EOF;
		else
			uio->uio_offset = tmpfs_dircookie(de);
	}
	return error;
}

/* --------------------------------------------------------------------- */

/*
 * Lookup a directory entry by its associated cookie.
 *
 * Must be called with the directory node locked (shared ok)
 */
struct lubycookie_info {
	off_t	cookie;
	struct tmpfs_dirent *de;
};

static int
lubycookie_cmp(struct tmpfs_dirent *de, void *arg)
{
	struct lubycookie_info *info = arg;
	off_t cookie = tmpfs_dircookie(de);

	if (cookie < info->cookie)
		return(-1);
	if (cookie > info->cookie)
		return(1);
	return(0);
}

static int
lubycookie_callback(struct tmpfs_dirent *de, void *arg)
{
	struct lubycookie_info *info = arg;

	if (tmpfs_dircookie(de) == info->cookie) {
		info->de = de;
		return(-1);
	}
	return(0);
}

struct tmpfs_dirent *
tmpfs_dir_lookupbycookie(struct tmpfs_node *node, off_t cookie)
{
	struct lubycookie_info info;

	info.cookie = cookie;
	info.de = NULL;
	RB_SCAN(tmpfs_dirtree_cookie, &node->tn_dir.tn_cookietree,
		lubycookie_cmp, lubycookie_callback, &info);
	return (info.de);
}

/* --------------------------------------------------------------------- */

/*
 * Helper function for tmpfs_readdir.  Returns as much directory entries
 * as can fit in the uio space.  The read starts at uio->uio_offset.
 * The function returns 0 on success, -1 if there was not enough space
 * in the uio structure to hold the directory entry or an appropriate
 * error code if another error happens.
 *
 * Caller must hold the node locked (shared ok)
 */
int
tmpfs_dir_getdents(struct tmpfs_node *node, struct uio *uio, off_t *cntp)
{
	int error;
	off_t startcookie;
	struct tmpfs_dirent *de;

	TMPFS_VALIDATE_DIR(node);

	/*
	 * Locate the first directory entry we have to return.  We have cached
	 * the last readdir in the node, so use those values if appropriate.
	 * Otherwise do a linear scan to find the requested entry.
	 */
	startcookie = uio->uio_offset;
	KKASSERT(startcookie != TMPFS_DIRCOOKIE_DOT);
	KKASSERT(startcookie != TMPFS_DIRCOOKIE_DOTDOT);

	if (startcookie == TMPFS_DIRCOOKIE_EOF)
		return 0;

	de = tmpfs_dir_lookupbycookie(node, startcookie);
	if (de == NULL)
		return EINVAL;

	/*
	 * Read as much entries as possible; i.e., until we reach the end of
	 * the directory or we exhaust uio space.
	 */
	do {
		ino_t d_ino;
		uint8_t d_type;

		/* Create a dirent structure representing the current
		 * tmpfs_node and fill it. */
		d_ino = de->td_node->tn_id;
		switch (de->td_node->tn_type) {
		case VBLK:
			d_type = DT_BLK;
			break;

		case VCHR:
			d_type = DT_CHR;
			break;

		case VDIR:
			d_type = DT_DIR;
			break;

		case VFIFO:
			d_type = DT_FIFO;
			break;

		case VLNK:
			d_type = DT_LNK;
			break;

		case VREG:
			d_type = DT_REG;
			break;

		case VSOCK:
			d_type = DT_SOCK;
			break;

		default:
			panic("tmpfs_dir_getdents: type %p %d",
			    de->td_node, (int)de->td_node->tn_type);
		}
		KKASSERT(de->td_namelen < 256); /* 255 + 1 */

		if (vop_write_dirent(&error, uio, d_ino, d_type,
		    de->td_namelen, de->td_name)) {
			error = -1;
			break;
		}

		(*cntp)++;
		de = RB_NEXT(tmpfs_dirtree_cookie,
			     node->tn_dir.tn_cookietree, de);
	} while (error == 0 && uio->uio_resid > 0 && de != NULL);

	/* Update the offset and cache. */
	if (de == NULL) {
		uio->uio_offset = TMPFS_DIRCOOKIE_EOF;
	} else {
		uio->uio_offset = tmpfs_dircookie(de);
	}

	return error;
}

/* --------------------------------------------------------------------- */

/*
 * Resizes the aobj associated to the regular file pointed to by vp to
 * the size newsize.  'vp' must point to a vnode that represents a regular
 * file.  'newsize' must be positive.
 *
 * pass NVEXTF_TRIVIAL when buf content will be overwritten, otherwise set 0
 * to be zero filled.
 *
 * Returns zero on success or an appropriate error code on failure.
 *
 * Caller must hold the node exclusively locked.
 */
int
tmpfs_reg_resize(struct vnode *vp, off_t newsize, int trivial)
{
	int error;
	vm_pindex_t newpages, oldpages;
	struct tmpfs_mount *tmp;
	struct tmpfs_node *node;
	off_t oldsize;
	int nvextflags;

#ifdef INVARIANTS
	KKASSERT(vp->v_type == VREG);
	KKASSERT(newsize >= 0);
#endif

	node = VP_TO_TMPFS_NODE(vp);
	tmp = VFS_TO_TMPFS(vp->v_mount);

	/*
	 * Convert the old and new sizes to the number of pages needed to
	 * store them.  It may happen that we do not need to do anything
	 * because the last allocated page can accommodate the change on
	 * its own.
	 */
	oldsize = node->tn_size;
	oldpages = round_page64(oldsize) / PAGE_SIZE;
	KKASSERT(oldpages == node->tn_reg.tn_aobj_pages);
	newpages = round_page64(newsize) / PAGE_SIZE;

	if (newpages > oldpages &&
	   tmp->tm_pages_used + newpages - oldpages > tmp->tm_pages_max) {
		error = ENOSPC;
		goto out;
	}
	node->tn_reg.tn_aobj_pages = newpages;
	node->tn_size = newsize;

	if (newpages != oldpages)
		atomic_add_long(&tmp->tm_pages_used, (newpages - oldpages));

	/*
	 * nvextflags to pass along for bdwrite() vs buwrite()
	 */
	if (vm_pages_needed || vm_paging_needed(0) ||
	    tmpfs_bufcache_mode >= 2) {
		nvextflags = 0;
	} else {
		nvextflags = NVEXTF_BUWRITE;
	}


	/*
	 * When adjusting the vnode filesize and its VM object we must
	 * also adjust our backing VM object (aobj).  The blocksize
	 * used must match the block sized we use for the buffer cache.
	 *
	 * The backing VM object may contain VM pages as well as swap
	 * assignments if we previously renamed main object pages into
	 * it during deactivation.
	 *
	 * To make things easier tmpfs uses a blksize in multiples of
	 * PAGE_SIZE, and will only increase the blksize as a small file
	 * increases in size.  Once a file has exceeded TMPFS_BLKSIZE (16KB),
	 * the blksize is maxed out.  Truncating the file does not reduce
	 * the blksize.
	 */
	if (newsize < oldsize) {
		vm_pindex_t osize;
		vm_pindex_t nsize;
		vm_object_t aobj;

		error = nvtruncbuf(vp, newsize, node->tn_blksize,
				   -1, nvextflags);
		aobj = node->tn_reg.tn_aobj;
		if (aobj) {
			osize = aobj->size;
			nsize = vp->v_object->size;
			if (nsize < osize) {
				aobj->size = osize;
				swap_pager_freespace(aobj, nsize,
						     osize - nsize);
				vm_object_page_remove(aobj, nsize, osize,
						      FALSE);
			}
		}
	} else {
		vm_object_t aobj;
		int nblksize;

		/*
		 * The first (and only the first) buffer in the file is resized
		 * in multiples of PAGE_SIZE, up to TMPFS_BLKSIZE.
		 */
		nblksize = node->tn_blksize;
		while (nblksize < TMPFS_BLKSIZE &&
		       nblksize < newsize) {
			nblksize += PAGE_SIZE;
		}

		if (trivial)
			nvextflags |= NVEXTF_TRIVIAL;

		error = nvextendbuf(vp, oldsize, newsize,
				    node->tn_blksize, nblksize,
				    -1, -1, nvextflags);
		node->tn_blksize = nblksize;
		aobj = node->tn_reg.tn_aobj;
		if (aobj)
			aobj->size = vp->v_object->size;
	}

out:
	return error;
}

/* --------------------------------------------------------------------- */

/*
 * Change flags of the given vnode.
 * Caller should execute tmpfs_update on vp after a successful execution.
 * The vnode must be locked on entry and remain locked on exit.
 */
int
tmpfs_chflags(struct vnode *vp, u_long vaflags, struct ucred *cred)
{
	int error;
	struct tmpfs_node *node;
	int flags;

	KKASSERT(vn_islocked(vp));

	node = VP_TO_TMPFS_NODE(vp);
	flags = node->tn_flags;

	/* Disallow this operation if the file system is mounted read-only. */
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;
	error = vop_helper_setattr_flags(&flags, vaflags, node->tn_uid, cred);

	/* Actually change the flags on the node itself */
	if (error == 0) {
		TMPFS_NODE_LOCK(node);
		node->tn_flags = flags;
		node->tn_status |= TMPFS_NODE_CHANGED;
		TMPFS_NODE_UNLOCK(node);
	}

	KKASSERT(vn_islocked(vp));

	return error;
}

/* --------------------------------------------------------------------- */

/*
 * Change access mode on the given vnode.
 * Caller should execute tmpfs_update on vp after a successful execution.
 * The vnode must be locked on entry and remain locked on exit.
 */
int
tmpfs_chmod(struct vnode *vp, mode_t vamode, struct ucred *cred)
{
	struct tmpfs_node *node;
	mode_t cur_mode;
	int error;

	KKASSERT(vn_islocked(vp));

	node = VP_TO_TMPFS_NODE(vp);

	/* Disallow this operation if the file system is mounted read-only. */
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;

	/* Immutable or append-only files cannot be modified, either. */
	if (node->tn_flags & (IMMUTABLE | APPEND))
		return EPERM;

	cur_mode = node->tn_mode;
	error = vop_helper_chmod(vp, vamode, cred, node->tn_uid, node->tn_gid,
				 &cur_mode);

	if (error == 0 &&
	    (node->tn_mode & ALLPERMS) != (cur_mode & ALLPERMS)) {
		TMPFS_NODE_LOCK(node);
		node->tn_mode &= ~ALLPERMS;
		node->tn_mode |= cur_mode & ALLPERMS;

		node->tn_status |= TMPFS_NODE_CHANGED;
		TMPFS_NODE_UNLOCK(node);
	}

	KKASSERT(vn_islocked(vp));

	return 0;
}

/* --------------------------------------------------------------------- */

/*
 * Change ownership of the given vnode.  At least one of uid or gid must
 * be different than VNOVAL.  If one is set to that value, the attribute
 * is unchanged.
 * Caller should execute tmpfs_update on vp after a successful execution.
 * The vnode must be locked on entry and remain locked on exit.
 */
int
tmpfs_chown(struct vnode *vp, uid_t uid, gid_t gid, struct ucred *cred)
{
	mode_t cur_mode;
	uid_t cur_uid;
	gid_t cur_gid;
	struct tmpfs_node *node;
	int error;

	KKASSERT(vn_islocked(vp));
	node = VP_TO_TMPFS_NODE(vp);

	/* Disallow this operation if the file system is mounted read-only. */
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;

	/* Immutable or append-only files cannot be modified, either. */
	if (node->tn_flags & (IMMUTABLE | APPEND))
		return EPERM;

	cur_uid = node->tn_uid;
	cur_gid = node->tn_gid;
	cur_mode = node->tn_mode;
	error = vop_helper_chown(vp, uid, gid, cred,
				 &cur_uid, &cur_gid, &cur_mode);

	if (error == 0) {
		TMPFS_NODE_LOCK(node);
		if (cur_uid != node->tn_uid ||
		    cur_gid != node->tn_gid ||
		    cur_mode != node->tn_mode) {
			node->tn_uid = cur_uid;
			node->tn_gid = cur_gid;
			node->tn_mode = cur_mode;
			node->tn_status |= TMPFS_NODE_CHANGED;
		}
		TMPFS_NODE_UNLOCK(node);
	}

	return error;
}

/* --------------------------------------------------------------------- */

/*
 * Change size of the given vnode.
 * Caller should execute tmpfs_update on vp after a successful execution.
 * The vnode must be locked on entry and remain locked on exit.
 */
int
tmpfs_chsize(struct vnode *vp, u_quad_t size, struct ucred *cred)
{
	int error;
	struct tmpfs_node *node;

	KKASSERT(vn_islocked(vp));

	node = VP_TO_TMPFS_NODE(vp);

	/* Decide whether this is a valid operation based on the file type. */
	error = 0;
	switch (vp->v_type) {
	case VDIR:
		return EISDIR;

	case VREG:
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return EROFS;
		break;

	case VBLK:
		/* FALLTHROUGH */
	case VCHR:
		/* FALLTHROUGH */
	case VFIFO:
		/* Allow modifications of special files even if in the file
		 * system is mounted read-only (we are not modifying the
		 * files themselves, but the objects they represent). */
		return 0;

	default:
		/* Anything else is unsupported. */
		return EOPNOTSUPP;
	}

	/* Immutable or append-only files cannot be modified, either. */
	if (node->tn_flags & (IMMUTABLE | APPEND))
		return EPERM;

	error = tmpfs_truncate(vp, size);
	/* tmpfs_truncate will raise the NOTE_EXTEND and NOTE_ATTRIB kevents
	 * for us, as will update tn_status; no need to do that here. */

	KKASSERT(vn_islocked(vp));

	return error;
}

/* --------------------------------------------------------------------- */

/*
 * Change access and modification times of the given vnode.
 * Caller should execute tmpfs_update on vp after a successful execution.
 * The vnode must be locked on entry and remain locked on exit.
 */
int
tmpfs_chtimes(struct vnode *vp, struct timespec *atime, struct timespec *mtime,
	      int vaflags, struct ucred *cred)
{
	struct tmpfs_node *node;

	KKASSERT(vn_islocked(vp));

	node = VP_TO_TMPFS_NODE(vp);

	/* Disallow this operation if the file system is mounted read-only. */
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;

	/* Immutable or append-only files cannot be modified, either. */
	if (node->tn_flags & (IMMUTABLE | APPEND))
		return EPERM;

	TMPFS_NODE_LOCK(node);
	if (atime->tv_sec != VNOVAL && atime->tv_nsec != VNOVAL)
		node->tn_status |= TMPFS_NODE_ACCESSED;

	if (mtime->tv_sec != VNOVAL && mtime->tv_nsec != VNOVAL) {
		node->tn_status |= TMPFS_NODE_MODIFIED;
		vclrflags(vp, VLASTWRITETS);
	}

	TMPFS_NODE_UNLOCK(node);

	tmpfs_itimes(vp, atime, mtime);

	KKASSERT(vn_islocked(vp));

	return 0;
}

/* --------------------------------------------------------------------- */
/* Sync timestamps */
void
tmpfs_itimes(struct vnode *vp, const struct timespec *acc,
	     const struct timespec *mod)
{
	struct tmpfs_node *node;
	struct timespec now;

	node = VP_TO_TMPFS_NODE(vp);

	if ((node->tn_status & (TMPFS_NODE_ACCESSED | TMPFS_NODE_MODIFIED |
	    TMPFS_NODE_CHANGED)) == 0) {
		return;
	}

	vfs_timestamp(&now);

	TMPFS_NODE_LOCK(node);
	if (node->tn_status & TMPFS_NODE_ACCESSED) {
		if (acc == NULL)
			 acc = &now;
		node->tn_atime = acc->tv_sec;
		node->tn_atimensec = acc->tv_nsec;
	}
	if (node->tn_status & TMPFS_NODE_MODIFIED) {
		if (mod == NULL)
			mod = &now;
		node->tn_mtime = mod->tv_sec;
		node->tn_mtimensec = mod->tv_nsec;
	}
	if (node->tn_status & TMPFS_NODE_CHANGED) {
		node->tn_ctime = now.tv_sec;
		node->tn_ctimensec = now.tv_nsec;
	}

	node->tn_status &= ~(TMPFS_NODE_ACCESSED |
			     TMPFS_NODE_MODIFIED |
			     TMPFS_NODE_CHANGED);
	TMPFS_NODE_UNLOCK(node);
}

/* --------------------------------------------------------------------- */

void
tmpfs_update(struct vnode *vp)
{
	tmpfs_itimes(vp, NULL, NULL);
}

/* --------------------------------------------------------------------- */

/*
 * Caller must hold an exclusive node lock.
 */
int
tmpfs_truncate(struct vnode *vp, off_t length)
{
	int error;
	struct tmpfs_node *node;

	node = VP_TO_TMPFS_NODE(vp);

	if (length < 0) {
		error = EINVAL;
		goto out;
	}

	if (node->tn_size == length) {
		error = 0;
		goto out;
	}

	if (length > VFS_TO_TMPFS(vp->v_mount)->tm_maxfilesize)
		return (EFBIG);


	error = tmpfs_reg_resize(vp, length, 1);

	if (error == 0)
		node->tn_status |= TMPFS_NODE_CHANGED | TMPFS_NODE_MODIFIED;

out:
	tmpfs_update(vp);

	return error;
}

/* --------------------------------------------------------------------- */

static ino_t
tmpfs_fetch_ino(struct tmpfs_mount *tmp)
{
	ino_t ret;

	ret = atomic_fetchadd_64(&tmp->tm_ino, 1);

	return (ret);
}

static int
tmpfs_dirtree_compare(struct tmpfs_dirent *a, struct tmpfs_dirent *b)
{
	if (a->td_namelen > b->td_namelen)
		return 1;
	else if (a->td_namelen < b->td_namelen)
		return -1;
	else
		return strncmp(a->td_name, b->td_name, a->td_namelen);
}

static int
tmpfs_dirtree_compare_cookie(struct tmpfs_dirent *a, struct tmpfs_dirent *b)
{
	if (a < b)
		return(-1);
	if (a > b)
		return(1);
	return 0;
}

/*
 * Lock for rename.  The namecache entries for the related terminal files
 * are already locked but the directories are not.  A directory lock order
 * reversal is possible so use a deterministic order.
 *
 * Generally order path parent-to-child or using a simple pointer comparison.
 * Probably not perfect but it should catch most of the cases.
 *
 * Underlying files must be locked after the related directory.
 */
void
tmpfs_lock4(struct tmpfs_node *node1, struct tmpfs_node *node2,
	    struct tmpfs_node *node3, struct tmpfs_node *node4)
{
	if (node1->tn_dir.tn_parent != node2 &&
	    (node1 < node2 || node2->tn_dir.tn_parent == node1)) {
		TMPFS_NODE_LOCK(node1);		/* fdir */
		TMPFS_NODE_LOCK(node3);		/* ffile */
		TMPFS_NODE_LOCK(node2);		/* tdir */
		if (node4)
			TMPFS_NODE_LOCK(node4);	/* tfile */
	} else {
		TMPFS_NODE_LOCK(node2);		/* tdir */
		if (node4)
			TMPFS_NODE_LOCK(node4);	/* tfile */
		TMPFS_NODE_LOCK(node1);		/* fdir */
		TMPFS_NODE_LOCK(node3);		/* ffile */
	}
}

void
tmpfs_unlock4(struct tmpfs_node *node1, struct tmpfs_node *node2,
	      struct tmpfs_node *node3, struct tmpfs_node *node4)
{
	if (node4)
		TMPFS_NODE_UNLOCK(node4);
	TMPFS_NODE_UNLOCK(node2);
	TMPFS_NODE_UNLOCK(node3);
	TMPFS_NODE_UNLOCK(node1);
}
