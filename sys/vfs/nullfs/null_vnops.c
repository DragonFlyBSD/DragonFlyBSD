/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * John Heidemann of the UCLA Ficus project.
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
 *	@(#)null_vnops.c	8.6 (Berkeley) 5/27/95
 *
 * Ancestors:
 *	@(#)lofs_vnops.c	1.2 (Berkeley) 6/18/92
 * $FreeBSD: src/sys/miscfs/nullfs/null_vnops.c,v 1.38.2.6 2002/07/31 00:32:28 semenu Exp $
 * $DragonFly: src/sys/vfs/nullfs/null_vnops.c,v 1.21 2004/12/17 00:18:30 dillon Exp $
 *	...and...
 *	@(#)null_vnodeops.c 1.20 92/07/07 UCLA Ficus project
 *
 * $FreeBSD: src/sys/miscfs/nullfs/null_vnops.c,v 1.38.2.6 2002/07/31 00:32:28 semenu Exp $
 */

/*
 * Null Layer
 *
 * (See mount_null(8) for more information.)
 *
 * The null layer duplicates a portion of the file system
 * name space under a new name.  In this respect, it is
 * similar to the loopback file system.  It differs from
 * the loopback fs in two respects:  it is implemented using
 * a stackable layers techniques, and its "null-node"s stack above
 * all lower-layer vnodes, not just over directory vnodes.
 *
 * The null layer has two purposes.  First, it serves as a demonstration
 * of layering by proving a layer which does nothing.  (It actually
 * does everything the loopback file system does, which is slightly
 * more than nothing.)  Second, the null layer can serve as a prototype
 * layer.  Since it provides all necessary layer framework,
 * new file system layers can be created very easily be starting
 * with a null layer.
 *
 * The remainder of this man page examines the null layer as a basis
 * for constructing new layers.
 *
 *
 * INSTANTIATING NEW NULL LAYERS
 *
 * New null layers are created with mount_null(8).
 * Mount_null(8) takes two arguments, the pathname
 * of the lower vfs (target-pn) and the pathname where the null
 * layer will appear in the namespace (alias-pn).  After
 * the null layer is put into place, the contents
 * of target-pn subtree will be aliased under alias-pn.
 *
 *
 * OPERATION OF A NULL LAYER
 *
 * The null layer is the minimum file system layer,
 * simply bypassing all possible operations to the lower layer
 * for processing there.  The majority of its activity centers
 * on the bypass routine, through which nearly all vnode operations
 * pass.
 *
 * The bypass routine accepts arbitrary vnode operations for
 * handling by the lower layer.  It begins by examing vnode
 * operation arguments and replacing any null-nodes by their
 * lower-layer equivlants.  It then invokes the operation
 * on the lower layer.  Finally, it replaces the null-nodes
 * in the arguments and, if a vnode is return by the operation,
 * stacks a null-node on top of the returned vnode.
 *
 * Although bypass handles most operations, vop_getattr, vop_lock,
 * vop_unlock, vop_inactive, vop_reclaim, and vop_print are not
 * bypassed. Vop_getattr must change the fsid being returned.
 * Vop_lock and vop_unlock must handle any locking for the
 * current vnode as well as pass the lock request down.
 * Vop_inactive and vop_reclaim are not bypassed so that
 * they can handle freeing null-layer specific data. Vop_print
 * is not bypassed to avoid excessive debugging information.
 * Also, certain vnode operations change the locking state within
 * the operation (create, mknod, remove, link, rename, mkdir, rmdir,
 * and symlink). Ideally these operations should not change the
 * lock state, but should be changed to let the caller of the
 * function unlock them. Otherwise all intermediate vnode layers
 * (such as union, umapfs, etc) must catch these functions to do
 * the necessary locking at their layer.
 *
 *
 * INSTANTIATING VNODE STACKS
 *
 * Mounting associates the null layer with a lower layer,
 * effect stacking two VFSes.  Vnode stacks are instead
 * created on demand as files are accessed.
 *
 * The initial mount creates a single vnode stack for the
 * root of the new null layer.  All other vnode stacks
 * are created as a result of vnode operations on
 * this or other null vnode stacks.
 *
 * New vnode stacks come into existance as a result of
 * an operation which returns a vnode.
 * The bypass routine stacks a null-node above the new
 * vnode before returning it to the caller.
 *
 * For example, imagine mounting a null layer with
 * "mount_null /usr/include /dev/layer/null".
 * Changing directory to /dev/layer/null will assign
 * the root null-node (which was created when the null layer was mounted).
 * Now consider opening "sys".  A vop_lookup would be
 * done on the root null-node.  This operation would bypass through
 * to the lower layer which would return a vnode representing
 * the UFS "sys".  Null_bypass then builds a null-node
 * aliasing the UFS "sys" and returns this to the caller.
 * Later operations on the null-node "sys" will repeat this
 * process when constructing other vnode stacks.
 *
 *
 * CREATING OTHER FILE SYSTEM LAYERS
 *
 * One of the easiest ways to construct new file system layers is to make
 * a copy of the null layer, rename all files and variables, and
 * then begin modifing the copy.  Sed can be used to easily rename
 * all variables.
 *
 * The umap layer is an example of a layer descended from the
 * null layer.
 *
 *
 * INVOKING OPERATIONS ON LOWER LAYERS
 *
 * There are two techniques to invoke operations on a lower layer
 * when the operation cannot be completely bypassed.  Each method
 * is appropriate in different situations.  In both cases,
 * it is the responsibility of the aliasing layer to make
 * the operation arguments "correct" for the lower layer
 * by mapping an vnode arguments to the lower layer.
 *
 * The first approach is to call the aliasing layer's bypass routine.
 * This method is most suitable when you wish to invoke the operation
 * currently being handled on the lower layer.  It has the advantage
 * that the bypass routine already must do argument mapping.
 * An example of this is null_getattrs in the null layer.
 *
 * A second approach is to directly invoke vnode operations on
 * the lower layer with the VOP_OPERATIONNAME interface.
 * The advantage of this method is that it is easy to invoke
 * arbitrary operations on the lower layer.  The disadvantage
 * is that vnode arguments must be manualy mapped.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include "null.h"

static int null_bug_bypass = 0;   /* for debugging: enables bypass printf'ing */
SYSCTL_INT(_debug, OID_AUTO, nullfs_bug_bypass, CTLFLAG_RW, 
	&null_bug_bypass, 0, "");

static int	null_nresolve(struct vop_nresolve_args *ap);
static int	null_ncreate(struct vop_ncreate_args *ap);
static int	null_nmkdir(struct vop_nmkdir_args *ap);
static int	null_nremove(struct vop_nremove_args *ap);
static int	null_nrmdir(struct vop_nrmdir_args *ap);
static int	null_nrename(struct vop_nrename_args *ap);

static int	null_revoke(struct vop_revoke_args *ap);
static int	null_access(struct vop_access_args *ap);
static int	null_createvobject(struct vop_createvobject_args *ap);
static int	null_destroyvobject(struct vop_destroyvobject_args *ap);
static int	null_getattr(struct vop_getattr_args *ap);
static int	null_getvobject(struct vop_getvobject_args *ap);
static int	null_inactive(struct vop_inactive_args *ap);
static int	null_islocked(struct vop_islocked_args *ap);
static int	null_lock(struct vop_lock_args *ap);
static int	null_lookup(struct vop_lookup_args *ap);
static int	null_open(struct vop_open_args *ap);
static int	null_print(struct vop_print_args *ap);
static int	null_reclaim(struct vop_reclaim_args *ap);
static int	null_rename(struct vop_rename_args *ap);
static int	null_setattr(struct vop_setattr_args *ap);
static int	null_unlock(struct vop_unlock_args *ap);

/*
 * This is the 10-Apr-92 bypass routine.
 *    This version has been optimized for speed, throwing away some
 * safety checks.  It should still always work, but it's not as
 * robust to programmer errors.
 *
 * In general, we map all vnodes going down and unmap them on the way back.
 * As an exception to this, vnodes can be marked "unmapped" by setting
 * the Nth bit in operation's vdesc_flags.
 *
 * Also, some BSD vnode operations have the side effect of vrele'ing
 * their arguments.  With stacking, the reference counts are held
 * by the upper node, not the lower one, so we must handle these
 * side-effects here.  This is not of concern in Sun-derived systems
 * since there are no such side-effects.
 *
 * This makes the following assumptions:
 * - only one returned vpp
 * - no INOUT vpp's (Sun's vop_open has one of these)
 * - the vnode operation vector of the first vnode should be used
 *   to determine what implementation of the op should be invoked
 * - all mapped vnodes are of our vnode-type (NEEDSWORK:
 *   problems on rmdir'ing mount points and renaming?)
 *
 * null_bypass(struct vnodeop_desc *a_desc, ...)
 */
int
null_bypass(struct vop_generic_args *ap)
{
	struct vnode **this_vp_p;
	int error;
	struct vnode *old_vps[VDESC_MAX_VPS];
	struct vnode **vps_p[VDESC_MAX_VPS];
	struct vnode ***vppp;
	struct vnodeop_desc *descp = ap->a_desc;
	int reles, i, j;

	if (null_bug_bypass)
		printf ("null_bypass: %s\n", descp->vdesc_name);

#ifdef DIAGNOSTIC
	/*
	 * We require at least one vp.
	 */
	if (descp->vdesc_vp_offsets == NULL ||
	    descp->vdesc_vp_offsets[0] == VDESC_NO_OFFSET)
		panic ("null_bypass: no vp's in map");
#endif

	/*
	 * Map the vnodes going in.
	 */
	reles = descp->vdesc_flags;
	for (i = 0; i < VDESC_MAX_VPS; ++i) {
		if (descp->vdesc_vp_offsets[i] == VDESC_NO_OFFSET)
			break;   /* bail out at end of list */
		vps_p[i] = this_vp_p =
			VOPARG_OFFSETTO(struct vnode**,descp->vdesc_vp_offsets[i],ap);
		/*
		 * We're not guaranteed that any but the first vnode
		 * are of our type.  Check for and don't map any
		 * that aren't.  (We must always map first vp or vclean fails.)
		 */
		if (i && (*this_vp_p == NULLVP ||
		    (*this_vp_p)->v_tag != VT_NULL)) {
			old_vps[i] = NULLVP;
		} else {
			old_vps[i] = *this_vp_p;
			*this_vp_p = NULLVPTOLOWERVP(*this_vp_p);
			/*
			 * Several operations have the side effect of vrele'ing
			 * their vp's.  We must account for that in the lower
			 * vp we pass down.
			 */
			if (reles & (VDESC_VP0_WILLRELE << i))
				vref(*this_vp_p);
		}

	}

	/*
	 * Call the operation on the lower layer with the modified
	 * argument structure.  We have to adjust a_fm to point to the
	 * lower vp's vop_ops structure.
	 */
	if (vps_p[0] && *vps_p[0]) {
		ap->a_ops = *(*(vps_p[0]))->v_ops;
		error = vop_vnoperate_ap(ap);
	} else {
		printf("null_bypass: no map for %s\n", descp->vdesc_name);
		error = EINVAL;
	}

	/*
	 * Maintain the illusion of call-by-value by restoring vnodes in the
	 * argument structure to their original value.
	 */
	reles = descp->vdesc_flags;
	for (i = 0; i < VDESC_MAX_VPS; ++i) {
		if (descp->vdesc_vp_offsets[i] == VDESC_NO_OFFSET)
			break;   /* bail out at end of list */
		if (old_vps[i]) {
			*(vps_p[i]) = old_vps[i];

			/*
			 * Since we operated on the lowervp's instead of the
			 * null node vp's, we have to adjust the null node
			 * vp's based on what the VOP did to the lower vp.
			 * 
			 * Note: the unlock case only occurs with rename.
			 * tdvp and tvp are both locked on call and must be
			 * unlocked on return.
			 *
			 * Unlock semantics indicate that if two locked vp's
			 * are passed and they are the same vp, they are only
			 * actually locked once.
			 */
			if (reles & (VDESC_VP0_WILLUNLOCK << i)) {
				VOP_UNLOCK(old_vps[i], LK_THISLAYER, curthread);
				for (j = i + 1; j < VDESC_MAX_VPS; ++j) {
					if (descp->vdesc_vp_offsets[j] == VDESC_NO_OFFSET)
						break;
					if (old_vps[i] == old_vps[j]) {
						reles &= ~(1 << (VDESC_VP0_WILLUNLOCK << j));
					}
				}
			}

			if (reles & (VDESC_VP0_WILLRELE << i))
				vrele(old_vps[i]);
		}
	}

	/*
	 * Map the possible out-going vpp
	 * (Assumes that the lower layer always returns
	 * a vref'ed vpp unless it gets an error.)
	 */
	if (descp->vdesc_vpp_offset != VDESC_NO_OFFSET &&
	    !(descp->vdesc_flags & VDESC_NOMAP_VPP) &&
	    !error) {
		/*
		 * XXX - even though some ops have vpp returned vp's,
		 * several ops actually vrele this before returning.
		 * We must avoid these ops.
		 * (This should go away when these ops are regularized.)
		 */
		if (descp->vdesc_flags & VDESC_VPP_WILLRELE)
			goto out;
		vppp = VOPARG_OFFSETTO(struct vnode***,
				 descp->vdesc_vpp_offset,ap);
		if (*vppp)
			error = null_node_create(old_vps[0]->v_mount, **vppp, *vppp);
	}

 out:
	return (error);
}

/*
 * We have to carry on the locking protocol on the null layer vnodes
 * as we progress through the tree. We also have to enforce read-only
 * if this layer is mounted read-only.
 *
 * null_lookup(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnp)
 */
static int
null_lookup(struct vop_lookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct vnode *dvp = ap->a_dvp;
	struct thread *td = cnp->cn_td;
	int flags = cnp->cn_flags;
	struct vnode *vp, *ldvp, *lvp;
	int error;

	if ((dvp->v_mount->mnt_flag & MNT_RDONLY) &&
	    (cnp->cn_nameiop == NAMEI_DELETE || 
	     cnp->cn_nameiop == NAMEI_RENAME)) {
		return (EROFS);
	}
	ldvp = NULLVPTOLOWERVP(dvp);

	/*
	 * If we are doing a ".." lookup we must release the lock on dvp
	 * now, before we run a lookup in the underlying fs, or we may 
	 * deadlock.  If we do this we must protect ldvp by ref'ing it.
	 */
	if (flags & CNP_ISDOTDOT) {
		vref(ldvp);
		VOP_UNLOCK(dvp, LK_THISLAYER, td);
	}

	/*
	 * Due to the non-deterministic nature of the handling of the
	 * parent directory lock by lookup, we cannot call null_bypass()
	 * here.  We must make a direct call.  It's faster to do a direct
	 * call, anyway.
	 */
	vp = lvp = NULL;
	error = VOP_LOOKUP(ldvp, &lvp, cnp);
	if (error == EJUSTRETURN && 
	    (dvp->v_mount->mnt_flag & MNT_RDONLY) &&
	    (cnp->cn_nameiop == NAMEI_CREATE || 
	     cnp->cn_nameiop == NAMEI_RENAME)) {
		error = EROFS;
	}

	if ((error == 0 || error == EJUSTRETURN) && lvp != NULL) {
		if (ldvp == lvp) {
			*ap->a_vpp = dvp;
			vref(dvp);
			vrele(lvp);
		} else {
			error = null_node_create(dvp->v_mount, lvp, &vp);
			if (error == 0)
				*ap->a_vpp = vp;
		}
	}

	/*
	 * The underlying fs will set PDIRUNLOCK if it unlocked the parent
	 * directory, which means we have to follow suit in the nullfs layer.
	 * Note that the parent directory may have already been unlocked due
	 * to the ".." case.  Note that use of cnp->cn_flags instead of flags.
	 */
	if (flags & CNP_ISDOTDOT) {
		if ((cnp->cn_flags & CNP_PDIRUNLOCK) == 0)
			VOP_LOCK(dvp, LK_THISLAYER | LK_EXCLUSIVE, td);
		vrele(ldvp);
	} else if (cnp->cn_flags & CNP_PDIRUNLOCK) {
		VOP_UNLOCK(dvp, LK_THISLAYER, td);
	}
	return (error);
}

/*
 * Setattr call. Disallow write attempts if the layer is mounted read-only.
 *
 * null_setattr(struct vnodeop_desc *a_desc, struct vnode *a_vp,
 *		struct vattr *a_vap, struct ucred *a_cred,
 *		struct thread *a_td)
 */
int
null_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;

  	if ((vap->va_flags != VNOVAL || vap->va_uid != (uid_t)VNOVAL ||
	    vap->va_gid != (gid_t)VNOVAL || vap->va_atime.tv_sec != VNOVAL ||
	    vap->va_mtime.tv_sec != VNOVAL || vap->va_mode != (mode_t)VNOVAL) &&
	    (vp->v_mount->mnt_flag & MNT_RDONLY))
		return (EROFS);
	if (vap->va_size != VNOVAL) {
 		switch (vp->v_type) {
 		case VDIR:
 			return (EISDIR);
 		case VCHR:
 		case VBLK:
 		case VSOCK:
 		case VFIFO:
			if (vap->va_flags != VNOVAL)
				return (EOPNOTSUPP);
			return (0);
		case VREG:
		case VLNK:
 		default:
			/*
			 * Disallow write attempts if the filesystem is
			 * mounted read-only.
			 */
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
		}
	}

	return (null_bypass(&ap->a_head));
}

/*
 *  We handle getattr only to change the fsid.
 *
 * null_getattr(struct vnode *a_vp, struct vattr *a_vap, struct ucred *a_cred,
 *		struct thread *a_td)
 */
static int
null_getattr(struct vop_getattr_args *ap)
{
	int error;

	if ((error = null_bypass(&ap->a_head)) != 0)
		return (error);

	ap->a_vap->va_fsid = ap->a_vp->v_mount->mnt_stat.f_fsid.val[0];
	return (0);
}

/*
 * Resolve a locked ncp at the nullfs layer.
 */
static int
null_nresolve(struct vop_nresolve_args *ap)
{
	return(vop_compat_nresolve(ap));
}

/*
 * Create a file
 */
static int
null_ncreate(struct vop_ncreate_args *ap)
{
	return(vop_compat_ncreate(ap));
}

static int
null_nmkdir(struct vop_nmkdir_args *ap)
{
	return(vop_compat_nmkdir(ap));
}

static int
null_nremove(struct vop_nremove_args *ap)
{
	return(vop_compat_nremove(ap));
}

static int
null_nrmdir(struct vop_nrmdir_args *ap)
{
	return(vop_compat_nrmdir(ap));
}

static int
null_nrename(struct vop_nrename_args *ap)
{
	return(vop_compat_nrename(ap));
}

/*
 * revoke is VX locked, we can't go through null_bypass
 */
static int
null_revoke(struct vop_revoke_args *ap)
{
	struct null_node *np;
	struct vnode *lvp;

	np = VTONULL(ap->a_vp);
	vx_unlock(ap->a_vp);
	if ((lvp = np->null_lowervp) != NULL) {
		vx_get(lvp);
		VOP_REVOKE(lvp, ap->a_flags);
		vx_put(lvp);
	}
	vx_lock(ap->a_vp);
	vgone(ap->a_vp);
	return(0);
}

/*
 * Handle to disallow write access if mounted read-only.
 *
 * null_access(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *		struct thread *a_td)
 */
static int
null_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	mode_t mode = ap->a_mode;

	/*
	 * Disallow write attempts on read-only layers;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the file system.
	 */
	if (mode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			break;
		}
	}
	return (null_bypass(&ap->a_head));
}

/*
 * We must handle open to be able to catch MNT_NODEV and friends.
 *
 * null_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	     struct thread *a_td)
 */
static int
null_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lvp = NULLVPTOLOWERVP(ap->a_vp);

	if ((vp->v_mount->mnt_flag & MNT_NODEV) &&
	    (lvp->v_type == VBLK || lvp->v_type == VCHR))
		return ENXIO;

	return (null_bypass(&ap->a_head));
}

/*
 * We handle this to eliminate null FS to lower FS
 * file moving. Don't know why we don't allow this,
 * possibly we should.
 *
 * null_rename(struct vnode *a_fdvp, struct vnode *a_fvp,
 *		struct componentname *a_fcnp, struct vnode *a_tdvp,
 *		struct vnode *a_tvp, struct componentname *a_tcnp)
 */
static int
null_rename(struct vop_rename_args *ap)
{
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *tvp = ap->a_tvp;

	/* Check for cross-device rename. */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		vrele(fdvp);
		vrele(fvp);
		return (EXDEV);
	}
	
	return (null_bypass(&ap->a_head));
}

/*
 * A special flag, LK_THISLAYER, causes the locking function to operate
 * ONLY on the nullfs layer.  Otherwise we are responsible for locking not
 * only our layer, but the lower layer as well.
 *
 * null_lock(struct vnode *a_vp, int a_flags, struct thread *a_td)
 */
static int
null_lock(struct vop_lock_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int flags = ap->a_flags;
	struct null_node *np = VTONULL(vp);
	struct vnode *lvp;
	int error;

	/*
	 * Lock the nullfs layer first, disposing of the interlock in the
	 * process.
	 */
	KKASSERT((flags & LK_INTERLOCK) == 0);
	error = lockmgr(&vp->v_lock, flags & ~LK_THISLAYER,
			NULL, ap->a_td);

	/*
	 * If locking only the nullfs layer, or if there is no lower layer,
	 * or if an error occured while attempting to lock the nullfs layer,
	 * we are done.
	 *
	 * np can be NULL is the vnode is being recycled from a previous
	 * hash collision.
	 */
	if ((flags & LK_THISLAYER) || np == NULL ||
	    np->null_lowervp == NULL || error) {
		return (error);
	}

	/*
	 * Lock the underlying vnode.  If we are draining we should not drain
	 * the underlying vnode, since it is not being destroyed, but we do
	 * lock it exclusively in that case.  Note that any interlocks have
	 * already been disposed of above.
	 */
	lvp = np->null_lowervp;
	if ((flags & LK_TYPE_MASK) == LK_DRAIN) {
		NULLFSDEBUG("null_lock: avoiding LK_DRAIN\n");
		error = vn_lock(lvp, (flags & ~LK_TYPE_MASK) | LK_EXCLUSIVE,
				ap->a_td);
	} else {
		error = vn_lock(lvp, flags, ap->a_td);
	}

	/*
	 * If an error occured we have to undo our nullfs lock, then return
	 * the original error.
	 */
	if (error)
		lockmgr(&vp->v_lock, LK_RELEASE, NULL, ap->a_td);
	return(error);
}

/*
 * A special flag, LK_THISLAYER, causes the unlocking function to operate
 * ONLY on the nullfs layer.  Otherwise we are responsible for unlocking not
 * only our layer, but the lower layer as well.
 *
 * null_unlock(struct vnode *a_vp, int a_flags, struct thread *a_td)
 */
static int
null_unlock(struct vop_unlock_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int flags = ap->a_flags;
	struct null_node *np = VTONULL(vp);
	struct vnode *lvp;
	int error;

	KKASSERT((flags & LK_INTERLOCK) == 0);
	/*
	 * nullfs layer only
	 */
	if (flags & LK_THISLAYER) {
		error = lockmgr(&vp->v_lock, 
				(flags & ~LK_THISLAYER) | LK_RELEASE,
				NULL, ap->a_td);
		return (error);
	}

	/*
	 * If there is no underlying vnode the lock operation occurs at
	 * the nullfs layer.  np can be NULL is the vnode is being recycled
	 * from a previous hash collision.
	 */
	if (np == NULL || (lvp = np->null_lowervp) == NULL) {
		error = lockmgr(&vp->v_lock, flags | LK_RELEASE,
				NULL, ap->a_td);
		return(error);
	}

	/*
	 * Unlock the lower layer first, then our nullfs layer.
	 */
	VOP_UNLOCK(lvp, flags, ap->a_td);
	error = lockmgr(&vp->v_lock, flags | LK_RELEASE, NULL, ap->a_td);
	return (error);
}

/*
 * null_islocked(struct vnode *a_vp, struct thread *a_td)
 *
 * If a lower layer exists return the lock status of the lower layer,
 * otherwise return the lock status of our nullfs layer.
 */
static int
null_islocked(struct vop_islocked_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lvp;
	struct null_node *np = VTONULL(vp);
	int error;

	lvp = np->null_lowervp;
	if (lvp == NULL)
		error = lockstatus(&vp->v_lock, ap->a_td);
	else
		error = VOP_ISLOCKED(lvp, ap->a_td);
	return (error);
}


/*
 * The vnode is no longer active.  However, the new VFS API may retain
 * the node in the vfs cache.  There is no way to tell that someone issued
 * a remove/rmdir operation on the underlying filesystem (yet), but we can't
 * remove the lowervp reference here.
 *
 * null_inactive(struct vnode *a_vp, struct thread *a_td)
 */
static int
null_inactive(struct vop_inactive_args *ap)
{
	/*struct vnode *vp = ap->a_vp;*/
	/*struct null_node *np = VTONULL(vp);*/

	/*
	 * At the moment don't do anything here.  All the rest of the code
	 * assumes that lowervp will remain inact, and the inactive nullvp
	 * may be reactivated at any time.  XXX I'm not sure why the 4.x code
	 * even worked.
	 */

	/*
	 * Now it is safe to release our nullfs layer vnode.
	 */
	return (0);
}

/*
 * We can free memory in null_inactive, but we do this
 * here. (Possible to guard vp->v_data to point somewhere)
 *
 * null_reclaim(struct vnode *a_vp, struct thread *a_td)
 */
static int
null_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lowervp;
	struct null_node *np;

	np = VTONULL(vp);
	vp->v_data = NULL;
	/*
	 * null_lowervp reference to lowervp.  The lower vnode's
	 * inactive routine may or may not be called when we do the
	 * final vrele().
	 */
	if (np) {
		null_node_rem(np);
		lowervp = np->null_lowervp;
		np->null_lowervp = NULLVP;
		if (lowervp)
			vrele(lowervp);
		free(np, M_NULLFSNODE);
	}
	return (0);
}

/*
 * null_print(struct vnode *a_vp)
 */
static int
null_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct null_node *np = VTONULL(vp);

	if (np == NULL) {
		printf ("\ttag VT_NULLFS, vp=%p, NULL v_data!\n", vp);
		return(0);
	}
	printf ("\ttag VT_NULLFS, vp=%p, lowervp=%p\n", vp, np->null_lowervp);
	if (np->null_lowervp != NULL) {
		printf("\tlowervp_lock: ");
		lockmgr_printinfo(&np->null_lowervp->v_lock);
	} else {
		printf("\tnull_lock: ");
		lockmgr_printinfo(&vp->v_lock);
	}
	printf("\n");
	return (0);
}

/*
 * Let an underlying filesystem do the work
 *
 * null_createvobject(struct vnode *vp, struct ucred *cred, struct proc *p)
 */
static int
null_createvobject(struct vop_createvobject_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lowervp = VTONULL(vp) ? NULLVPTOLOWERVP(vp) : NULL;
	int error;

	if (vp->v_type == VNON || lowervp == NULL)
		return 0;
	error = VOP_CREATEVOBJECT(lowervp, ap->a_td);
	if (error)
		return (error);
	vp->v_flag |= VOBJBUF;
	return (0);
}

/*
 * We have nothing to destroy and this operation shouldn't be bypassed.
 *
 * null_destroyvobject(struct vnode *vp)
 */
static int
null_destroyvobject(struct vop_destroyvobject_args *ap)
{
	struct vnode *vp = ap->a_vp;

	vp->v_flag &= ~VOBJBUF;
	return (0);
}

/*
 * null_getvobject(struct vnode *vp, struct vm_object **objpp)
 *
 * Note that this can be called when a vnode is being recycled, and
 * v_data may be NULL in that case if nullfs had to recycle a vnode
 * due to a null_node collision.
 */
static int
null_getvobject(struct vop_getvobject_args *ap)
{
	struct vnode *lvp;

	if (ap->a_vp->v_data == NULL)
		return EINVAL;

	lvp = NULLVPTOLOWERVP(ap->a_vp);
	if (lvp == NULL)
		return EINVAL;
	return (VOP_GETVOBJECT(lvp, ap->a_objpp));
}

/*
 * Global vfs data structures
 */
struct vnodeopv_entry_desc null_vnodeop_entries[] = {
	{ &vop_default_desc,		(void *) null_bypass },
	{ &vop_access_desc,		(void *) null_access },
	{ &vop_createvobject_desc,	(void *) null_createvobject },
	{ &vop_destroyvobject_desc,	(void *) null_destroyvobject },
	{ &vop_getattr_desc,		(void *) null_getattr },
	{ &vop_getvobject_desc,		(void *) null_getvobject },
	{ &vop_inactive_desc,		(void *) null_inactive },
	{ &vop_islocked_desc,		(void *) null_islocked },
	{ &vop_lock_desc,		(void *) null_lock },
	{ &vop_lookup_desc,		(void *) null_lookup },
	{ &vop_open_desc,		(void *) null_open },
	{ &vop_print_desc,		(void *) null_print },
	{ &vop_reclaim_desc,		(void *) null_reclaim },
	{ &vop_rename_desc,		(void *) null_rename },
	{ &vop_setattr_desc,		(void *) null_setattr },
	{ &vop_unlock_desc,		(void *) null_unlock },
	{ &vop_revoke_desc,		(void *) null_revoke },

	{ &vop_nresolve_desc,		(void *) null_nresolve },
	{ &vop_ncreate_desc,		(void *) null_ncreate },
	{ &vop_nmkdir_desc,		(void *) null_nmkdir },
	{ &vop_nremove_desc,		(void *) null_nremove },
	{ &vop_nrmdir_desc,		(void *) null_nrmdir },
	{ &vop_nrename_desc,		(void *) null_nrename },
	{ NULL, NULL }
};

