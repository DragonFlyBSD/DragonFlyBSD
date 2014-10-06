/*
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)ufs_vnops.c 8.27 (Berkeley) 5/27/95
 *	@(#)ext2_vnops.c	8.7 (Berkeley) 2/3/94
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_vnops.c,v 1.51.2.2 2003/01/02 17:26:18 bde Exp $
 */

#include "opt_quota.h"
#include "opt_suiddir.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/lockf.h>
#include <sys/event.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/jail.h>
#include <sys/namei.h>
#include <sys/signalvar.h>
#include <sys/unistd.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>
#include <vm/vnode_pager.h>

#include <sys/buf2.h>
#include <sys/thread2.h>

#include <vfs/fifofs/fifo.h>

#include "dir.h"
#include "quota.h"
#include "inode.h"
#include "ext2mount.h"
#include "ext2_fs_sb.h"
#include "fs.h"
#include "ext2_extern.h"
#include "ext2_fs.h"

static int ext2_access (struct vop_access_args *);
static int ext2_advlock (struct vop_advlock_args *);
static int ext2_chmod (struct vnode *, int, struct ucred *);
static int ext2_chown (struct vnode *, uid_t, gid_t, struct ucred *);
static int ext2_close (struct vop_close_args *);
static int ext2_getattr (struct vop_getattr_args *);
static int ext2_makeinode (int mode, struct vnode *, struct vnode **, struct componentname *);
static int ext2_mmap (struct vop_mmap_args *);
static int ext2_open (struct vop_open_args *);
static int ext2_pathconf (struct vop_pathconf_args *);
static int ext2_print (struct vop_print_args *);
static int ext2_readlink (struct vop_readlink_args *);
static int ext2_setattr (struct vop_setattr_args *);
static int ext2_strategy (struct vop_strategy_args *);
static int ext2_whiteout (struct vop_old_whiteout_args *);
static int filt_ext2read (struct knote *kn, long hint);
static int filt_ext2write (struct knote *kn, long hint);
static int filt_ext2vnode (struct knote *kn, long hint);
static void filt_ext2detach (struct knote *kn);
static int ext2_kqfilter (struct vop_kqfilter_args *ap);
static int ext2fifo_close (struct vop_close_args *);
static int ext2fifo_kqfilter (struct vop_kqfilter_args *);
static int ext2fifo_read (struct vop_read_args *);
static int ext2fifo_write (struct vop_write_args *);

static int ext2_fsync (struct vop_fsync_args *);
static int ext2_read (struct vop_read_args *);
static int ext2_write (struct vop_write_args *);
static int ext2_remove (struct vop_old_remove_args *);
static int ext2_link (struct vop_old_link_args *);
static int ext2_rename (struct vop_old_rename_args *);
static int ext2_mkdir (struct vop_old_mkdir_args *);
static int ext2_rmdir (struct vop_old_rmdir_args *);
static int ext2_create (struct vop_old_create_args *);
static int ext2_mknod (struct vop_old_mknod_args *);
static int ext2_symlink (struct vop_old_symlink_args *);

#include "ext2_readwrite.c"

union _qcvt {
	int64_t qcvt;
	int32_t val[2];
};
#define SETHIGH(q, h) { \
	union _qcvt tmp; \
	tmp.qcvt = (q); \
	tmp.val[_QUAD_HIGHWORD] = (h); \
	(q) = tmp.qcvt; \
}
#define SETLOW(q, l) { \
	union _qcvt tmp; \
	tmp.qcvt = (q); \
	tmp.val[_QUAD_LOWWORD] = (l); \
	(q) = tmp.qcvt; \
}
#define VN_KNOTE(vp, b) \
	KNOTE(&vp->v_pollinfo.vpi_kqinfo.ki_note, (b))

#define OFSFMT(vp)		((vp)->v_mount->mnt_maxsymlinklen <= 0)

/*
 * A virgin directory (no blushing please).
 * Note that the type and namlen fields are reversed relative to ufs.
 * Also, we don't use `struct odirtemplate', since it would just cause
 * endianness problems.
 */
static struct dirtemplate ext2_mastertemplate = {
	0, 12, 1, EXT2_FT_DIR, ".",
	0, DIRBLKSIZ - 12, 2, EXT2_FT_DIR, ".."
};
static struct dirtemplate ext2_omastertemplate = {
	0, 12, 1, EXT2_FT_UNKNOWN, ".",
	0, DIRBLKSIZ - 12, 2, EXT2_FT_UNKNOWN, ".."
};

/*
 * Create a regular file
 *
 * ext2_create(struct vnode *a_dvp, struct vnode **a_vpp,
 *	       struct componentname *a_cnp, struct vattr *a_vap)
 */
static int
ext2_create(struct vop_old_create_args *ap)
{
	int error;

	error =
	    ext2_makeinode(MAKEIMODE(ap->a_vap->va_type, ap->a_vap->va_mode),
	    ap->a_dvp, ap->a_vpp, ap->a_cnp);
	if (error)
		return (error);
	return (0);
}

/*
 * Synch an open file.
 *
 * ext2_fsync(struct vnode *a_vp, struct ucred *a_cred, int a_waitfor,
 *	      struct proc *a_p)
 */
/* ARGSUSED */

static int ext2_fsync_bp(struct buf *bp, void *data);

struct ext2_fsync_bp_info {
	struct vnode *vp;
	int waitfor;
};

static int
ext2_fsync(struct vop_fsync_args *ap)
{
	struct ext2_fsync_bp_info info;
	struct vnode *vp = ap->a_vp;
	int count;

	/*
	 * XXX why is all this fs specific?
	 */

	/*
	 * Flush all dirty buffers associated with a vnode.
	 */
	ext2_discard_prealloc(VTOI(vp));

	lwkt_gettoken(&vp->v_token);
	info.vp = vp;
loop:
	info.waitfor = ap->a_waitfor;
	count = RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, NULL,
			ext2_fsync_bp, &info);
	if (count)
		goto loop;

	if (ap->a_waitfor == MNT_WAIT) {
		bio_track_wait(&vp->v_track_write, 0, 0);
#if DIAGNOSTIC
		if (!RB_EMPTY(&vp->v_rbdirty_tree)) {
			vprint("ext2_fsync: dirty", vp);
			goto loop;
		}
#endif
	}
	lwkt_reltoken(&vp->v_token);
	return (EXT2_UPDATE(ap->a_vp, ap->a_waitfor == MNT_WAIT));
}

static int
ext2_fsync_bp(struct buf *bp, void *data)
{
	struct ext2_fsync_bp_info *info = data;

	if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT))
		return(0);
	if ((bp->b_flags & B_DELWRI) == 0)
		panic("ext2_fsync: not dirty");
	bremfree(bp);

	/*
	 * Wait for I/O associated with indirect blocks to complete,
	 * since there is no way to quickly wait for them below.
	 */
	if (bp->b_vp == info->vp || (info->waitfor & MNT_NOWAIT))
		bawrite(bp);
	else
		bwrite(bp);
	return(1);
}

/*
 * Mknod vnode call
 *
 * ext2_mknod(struct vnode *a_dvp, struct vnode **a_vpp,
 *	      struct componentname *a_cnp, struct vattr *a_vap)
 */
/* ARGSUSED */
static int
ext2_mknod(struct vop_old_mknod_args *ap)
{
	struct vattr *vap = ap->a_vap;
	struct vnode **vpp = ap->a_vpp;
	struct inode *ip;
	ino_t ino;
	int error;

	if (major(vap->va_rdev) != VNOVAL &&
	    makeudev(major(vap->va_rdev), minor(vap->va_rdev)) == NOUDEV) {
		return (EINVAL);
	}

	error = ext2_makeinode(MAKEIMODE(vap->va_type, vap->va_mode),
	    ap->a_dvp, vpp, ap->a_cnp);
	if (error)
		return (error);
	ip = VTOI(*vpp);
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	if (major(vap->va_rdev) != VNOVAL) {
		/*
		 * Want to be able to use this to make badblock
		 * inodes, so don't truncate the dev number.
		 */
		ip->i_rdev = makeudev(major(vap->va_rdev), minor(vap->va_rdev));
	}
	/*
	 * Remove inode, then reload it through VFS_VGET so it is
	 * checked to see if it is an alias of an existing entry in
	 * the inode cache.
	 */
	(*vpp)->v_type = VNON;
	ino = ip->i_number;	/* Save this before vgone() invalidates ip. */
	vgone_vxlocked(*vpp);
	vput(*vpp);
	error = VFS_VGET(ap->a_dvp->v_mount, NULL, ino, vpp);
	if (error) {
		*vpp = NULL;
		return (error);
	}
	return (0);
}

/*
 * ext2_remove(struct vnode *a_dvp, struct vnode *a_vp,
 *	       struct componentname *a_cnp)
 */
static int
ext2_remove(struct vop_old_remove_args *ap)
{
	struct inode *ip;
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	int error;

	ip = VTOI(vp);
	if ((ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (VTOI(dvp)->i_flags & APPEND)) {
		error = EPERM;
		goto out;
	}
	error = ext2_dirremove(dvp, ap->a_cnp);
	if (error == 0) {
		ip->i_nlink--;
		ip->i_flag |= IN_CHANGE;
	}
out:
	return (error);
}

/*
 * link vnode call
 *
 * ext2_link(struct vnode *a_tdvp, struct vnode *a_vp,
 *	     struct componentname *a_cnp)
 */
static int
ext2_link(struct vop_old_link_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *tdvp = ap->a_tdvp;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip;
	int error;

	if (tdvp->v_mount != vp->v_mount) {
		error = EXDEV;
		goto out2;
	}
	if (tdvp != vp) {
		error = vn_lock(vp, LK_EXCLUSIVE | LK_FAILRECLAIM);
		if (error)
			goto out2;
	}
	ip = VTOI(vp);
	if ((nlink_t)ip->i_nlink >= LINK_MAX) {
		error = EMLINK;
		goto out1;
	}
	if (ip->i_flags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto out1;
	}
	ip->i_nlink++;
	ip->i_flag |= IN_CHANGE;
	error = EXT2_UPDATE(vp, 1);
	if (!error)
		error = ext2_direnter(ip, tdvp, cnp);
	if (error) {
		ip->i_nlink--;
		ip->i_flag |= IN_CHANGE;
	}
out1:
	if (tdvp != vp)
		vn_unlock(vp);
out2:
	return (error);
}

/*
 * Rename system call.  fdvp, fvp are ref'd.  tvp, tdvp are ref'd and locked.
 * all vp's are released and must be in an unlocked state on return.
 *
 * ext2_rename(struct vnode *a_fdvp, struct vnode *a_fvp,
 *		struct componentname *a_fcnp, struct vnode *a_tdvp,
 *		struct vnode *a_tvp, struct componentname *a_tcnp)
 */
static int
ext2_rename(struct vop_old_rename_args *ap)
{
	struct vnode *tvp = ap->a_tvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct inode *ip, *xp, *dp;
	struct dirtemplate dirbuf;
	int doingdirectory = 0, oldparent = 0, newparent = 0;
	int error = 0;
	u_char namlen;

	/*
	 * Check for cross-device rename.
	 */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount)) ||
	    tvp == tdvp) {
		error = EXDEV;
abortit:
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		vrele(fdvp);
		vrele(fvp);
		return (error);
	}

	if (tvp && ((VTOI(tvp)->i_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (VTOI(tdvp)->i_flags & APPEND))) {
		error = EPERM;
		goto abortit;
	}

	/*
	 * Renaming a file to itself has no effect.  The upper layers should
	 * not call us in that case.  Temporarily just warn if they do.
	 */
	if (fvp == tvp) {
		error = 0;
		goto abortit;
	}

	error = vn_lock(fvp, LK_EXCLUSIVE | LK_FAILRECLAIM);
	if (error)
		goto abortit;

	/*
	 * fvp, tvp, tdvp locked.  fdvp not locked but note that fdvp may
	 * be equal to tdvp.
	 */
	dp = VTOI(fdvp);
	ip = VTOI(fvp);
	if (ip->i_nlink >= LINK_MAX) {
		vn_unlock(fvp);
		error = EMLINK;
		goto abortit;
	}
	if ((ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND))
	    || (dp->i_flags & APPEND)) {
		vn_unlock(fvp);
		error = EPERM;
		goto abortit;
	}
	if ((ip->i_mode & IFMT) == IFDIR) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */
		if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
		    dp == ip || (fcnp->cn_flags | tcnp->cn_flags) & CNP_ISDOTDOT ||
		    (ip->i_flag & IN_RENAME)) {
			vn_unlock(fvp);
			error = EINVAL;
			goto abortit;
		}
		ip->i_flag |= IN_RENAME;
		oldparent = dp->i_number;
		doingdirectory++;
	}

	/*
	 * tvp is non-NULL if the target exists.   fvp is still locked but
	 * we will unlock it soon.  The 'bad' goto target requires dp and
	 * xp to be correctly assigned.
	 */
	dp = VTOI(tdvp);
	if (tvp)
		xp = VTOI(tvp);
	else
		xp = NULL;

	/*
	 * 1) Bump link count while we're moving stuff
	 *    around.  If we crash somewhere before
	 *    completing our work, the link count
	 *    may be wrong, but correctable.
	 */
	ip->i_nlink++;
	ip->i_flag |= IN_CHANGE;
	if ((error = EXT2_UPDATE(fvp, 1)) != 0) {
		vn_unlock(fvp);
		goto bad;
	}

	/*
	 * If ".." must be changed (ie the directory gets a new
	 * parent) then the source directory must not be in the
	 * directory heirarchy above the target, as this would
	 * orphan everything below the source directory. Also
	 * the user must have write permission in the source so
	 * as to be able to change "..". We must repeat the call
	 * to namei, as the parent directory is unlocked by the
	 * call to checkpath().
	 */
	error = VOP_EACCESS(fvp, VWRITE, tcnp->cn_cred);
	vn_unlock(fvp);

	/*
	 * tvp (if not NULL) and tdvp are locked.  fvp and fdvp are not.
	 * dp and xp are set according to tdvp and tvp.
	 */
	if (oldparent != dp->i_number)
		newparent = dp->i_number;
	if (doingdirectory && newparent) {
		if (error)	/* write access check above */
			goto bad;

		/*
		 * Prepare for relookup, get rid of xp
		 */
		if (xp != NULL) {
			vput(tvp);
			xp = NULL;
		}

		/*
		 * checkpath vput()'s tdvp (VTOI(dp)) on return no matter what,
		 * get an extra ref so we wind up with just an unlocked, ref'd
		 * tdvp.  The 'out' target skips xp and tdvp cleanups.  Our
		 * tdvp is now unlocked so we have to clean it up ourselves.
		 */
		vref(tdvp);
		error = ext2_checkpath(ip, dp, tcnp->cn_cred);
		tcnp->cn_flags |= CNP_PDIRUNLOCK;
		if (error) {
			vrele(tdvp);
			goto out;
		}
		/*
		 * relookup no longer messes with the ref count.  An unlocked
		 * tdvp must be passed and if no error occurs a locked tdvp
		 * will be returned.  We have to use the out target again.
		 */
		error = relookup(tdvp, &tvp, tcnp);
		if (error) {
			if (tcnp->cn_flags & CNP_PDIRUNLOCK)
				vrele(tdvp);
			else
				vput(tdvp);
			goto out;
		}

		/*
		 * tdvp is locked at this point.  in the RENAME case tvp may
		 * be NULL without an error, assign xp accordingly.  The
		 * 'bad' target can be used again after this.
		 */
		dp = VTOI(tdvp);
		if (tvp)
			xp = VTOI(tvp);
	}
	/*
	 * 2) If target doesn't exist, link the target
	 *    to the source and unlink the source.
	 *    Otherwise, rewrite the target directory
	 *    entry to reference the source inode and
	 *    expunge the original entry's existence.
	 *
	 * tdvp and tvp are cleaned up by this code.  tvp is only good if
	 * xp is not NULL.
	 */
	if (xp == NULL) {
		if (dp->i_dev != ip->i_dev)
			panic("ext2_rename: EXDEV");
		/*
		 * Account for ".." in new directory.
		 * When source and destination have the same
		 * parent we don't fool with the link count.
		 */
		if (doingdirectory && newparent) {
			if ((nlink_t)dp->i_nlink >= LINK_MAX) {
				error = EMLINK;
				goto bad;
			}
			dp->i_nlink++;
			dp->i_flag |= IN_CHANGE;
			error = EXT2_UPDATE(tdvp, 1);
			if (error)
				goto bad;
		}
		error = ext2_direnter(ip, tdvp, tcnp);
		if (error) {
			if (doingdirectory && newparent) {
				dp->i_nlink--;
				dp->i_flag |= IN_CHANGE;
				EXT2_UPDATE(tdvp, 1);
			}
			goto bad;
		}

		/*
		 * manual cleanup, we can't use the bad or out target after
		 * this.
		 */
		vput(tdvp);
	} else {
		if (xp->i_dev != dp->i_dev || xp->i_dev != ip->i_dev)
			panic("ext2_rename: EXDEV");
		/*
		 * Short circuit rename(foo, foo).
		 */
		if (xp->i_number == ip->i_number)
			panic("ext2_rename: same file");
		/*
		 * If the parent directory is "sticky", then the user must
		 * own the parent directory, or the destination of the rename,
		 * otherwise the destination may not be changed (except by
		 * root). This implements append-only directories.
		 */
		if ((dp->i_mode & S_ISTXT) && tcnp->cn_cred->cr_uid != 0 &&
		    tcnp->cn_cred->cr_uid != dp->i_uid &&
		    xp->i_uid != tcnp->cn_cred->cr_uid) {
			error = EPERM;
			goto bad;
		}
		/*
		 * Target must be empty if a directory and have no links
		 * to it. Also, ensure source and target are compatible
		 * (both directories, or both not directories).
		 */
		if ((xp->i_mode&IFMT) == IFDIR) {
			if (! ext2_dirempty(xp, dp->i_number, tcnp->cn_cred) ||
			    xp->i_nlink > 2) {
				error = ENOTEMPTY;
				goto bad;
			}
			if (!doingdirectory) {
				error = ENOTDIR;
				goto bad;
			}
		} else if (doingdirectory) {
			error = EISDIR;
			goto bad;
		}
		error = ext2_dirrewrite(dp, ip, tcnp);
		if (error)
			goto bad;
		/*
		 * If the target directory is in the same
		 * directory as the source directory,
		 * decrement the link count on the parent
		 * of the target directory.
		 */
		if (doingdirectory && !newparent) {
			dp->i_nlink--;
			dp->i_flag |= IN_CHANGE;
		}

		/*
		 * manual cleanup, we can't use the bad or out target after
		 * this.
		 */
		vput(tdvp);

		/*
		 * Adjust the link count of the target to
		 * reflect the dirrewrite above.  If this is
		 * a directory it is empty and there are
		 * no links to it, so we can squash the inode and
		 * any space associated with it.  We disallowed
		 * renaming over top of a directory with links to
		 * it above, as the remaining link would point to
		 * a directory without "." or ".." entries.
		 */
		xp->i_nlink--;
		if (doingdirectory) {
			if (--xp->i_nlink != 0)
				panic("ext2_rename: linked directory");
			error = EXT2_TRUNCATE(tvp, (off_t)0, IO_SYNC,
					      tcnp->cn_cred);
		}
		xp->i_flag |= IN_CHANGE;
		vput(tvp);
		xp = NULL;
	}

	/*
	 * tvp and tdvp have been cleaned up.  The bad and out targets may
	 * not be used.  fvp and fdvp are ref'd but not locked.  ip
	 * still represents the old fvp and ip->i_flag may still have IN_RENAME
	 * set (if doingdirectory).
	 */

	/*
	 * 3) Unlink the source.
	 *
	 * fdvp is locked and ref'd. ap->a_fvp holds the old lookup unlocked
	 * and ref'd, fvp will hold the new lookup locked and ref'd.
	 *
	 * After the relookup ap->a_fvp must be released as part of our
	 * cleanup, not just fdvp and fvp.  And, on success, fdvp and
	 * fvp will be locked so the bad and out targets cannot be used.
	 */
	fcnp->cn_flags &= ~CNP_MODMASK;
	fcnp->cn_flags |= CNP_LOCKPARENT;
	KKASSERT(fcnp->cn_flags & CNP_PDIRUNLOCK);
	error = relookup(fdvp, &fvp, fcnp);
	if (error) {
		/*
		 * From name has disappeared.
		 */
		if (doingdirectory)
			panic("ext2_rename: lost dir entry");
		/* ip->i_flag only sets IN_RENAME if doingdirectory */
		vrele(ap->a_fvp);
		if (fcnp->cn_flags & CNP_PDIRUNLOCK)
			vrele(fdvp);
		else
			vput(fdvp);
		return (0);
	}
	KKASSERT((fcnp->cn_flags & CNP_PDIRUNLOCK) == 0);

	/*
	 * This case shouldn't occur
	 */
	if (fvp == NULL) {
		/*
		 * From name has disappeared.
		 */
		if (doingdirectory)
			panic("ext2_rename: lost dir entry");
		/* ip->i_flag only sets IN_RENAME if doingdirectory */
		vrele(ap->a_fvp);
		vput(fvp);
		vput(fdvp);
		return (0);
	}

	/*
	 * fvp and fdvp are both ref'd and locked.
	 */
	xp = VTOI(fvp);
	dp = VTOI(fdvp);

	/*
	 * Ensure that the directory entry still exists and has not
	 * changed while the new name has been entered. If the source is
	 * a file then the entry may have been unlinked or renamed. In
	 * either case there is no further work to be done. If the source
	 * is a directory then it cannot have been rmdir'ed; its link
	 * count of three would cause a rmdir to fail with ENOTEMPTY.
	 * The IN_RENAME flag ensures that it cannot be moved by another
	 * rename.
	 */
	if (xp != ip) {
		if (doingdirectory)
			panic("ext2_rename: lost dir entry");
		/* ip->i_flag only sets IN_RENAME if doingdirectory */
	} else {
		/*
		 * If the source is a directory with a
		 * new parent, the link count of the old
		 * parent directory must be decremented
		 * and ".." set to point to the new parent.
		 */
		if (doingdirectory && newparent) {
			dp->i_nlink--;
			dp->i_flag |= IN_CHANGE;
			error = vn_rdwr(UIO_READ, fvp, (caddr_t)&dirbuf,
					sizeof (struct dirtemplate), (off_t)0,
					UIO_SYSSPACE, IO_NODELOCKED,
					tcnp->cn_cred, NULL);
			if (error == 0) {
				/* Like ext2 little-endian: */
				namlen = dirbuf.dotdot_type;
				if (namlen != 2 ||
				    dirbuf.dotdot_name[0] != '.' ||
				    dirbuf.dotdot_name[1] != '.') {
					ext2_dirbad(xp, (doff_t)12,
					    "rename: mangled dir");
				} else {
					dirbuf.dotdot_ino = newparent;
					vn_rdwr(UIO_WRITE, fvp,
						(caddr_t)&dirbuf,
						sizeof (struct dirtemplate),
						(off_t)0, UIO_SYSSPACE,
						IO_NODELOCKED|IO_SYNC,
						tcnp->cn_cred, NULL);
				}
			}
		}
		error = ext2_dirremove(fdvp, fcnp);
		if (!error) {
			xp->i_nlink--;
			xp->i_flag |= IN_CHANGE;
		}
		xp->i_flag &= ~IN_RENAME;
	}
	vput(fdvp);
	vput(fvp);
	vrele(ap->a_fvp);
	return (error);

bad:
	if (xp)
		vput(ITOV(xp));
	if (dp)
		vput(ITOV(dp));
out:
	if (doingdirectory)
		ip->i_flag &= ~IN_RENAME;

	error = vn_lock(fvp, LK_EXCLUSIVE);
	if (error == 0) {
		ip->i_nlink--;
		ip->i_flag |= IN_CHANGE;
		ip->i_flag &= ~IN_RENAME;
		vput(fvp);
	} else {
		vrele(fvp);
	}
	return (error);
}

/*
 * Mkdir system call
 *
 * ext2_mkdir(struct vnode *a_dvp, struct vnode **a_vpp,
 *	      struct componentname *a_cnp, struct vattr *a_vap)
 */
static int
ext2_mkdir(struct vop_old_mkdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip, *dp;
	struct vnode *tvp;
	struct dirtemplate dirtemplate, *dtp;
	int error, dmode;

	dp = VTOI(dvp);
	if ((nlink_t)dp->i_nlink >= LINK_MAX) {
		error = EMLINK;
		goto out;
	}
	dmode = vap->va_mode & 0777;
	dmode |= IFDIR;
	/*
	 * Must simulate part of ext2_makeinode here to acquire the inode,
	 * but not have it entered in the parent directory. The entry is
	 * made later after writing "." and ".." entries.
	 */
	error = EXT2_VALLOC(dvp, dmode, cnp->cn_cred, &tvp);
	if (error)
		goto out;
	ip = VTOI(tvp);
	ip->i_gid = dp->i_gid;
#ifdef SUIDDIR
	{
#ifdef QUOTA
		struct ucred ucred, *ucp;
		ucp = cnp->cn_cred;
#endif
		/*
		 * if we are hacking owners here, (only do this where told to)
		 * and we are not giving it TOO root, (would subvert quotas)
		 * then go ahead and give it to the other user.
		 * The new directory also inherits the SUID bit.
		 * If user's UID and dir UID are the same,
		 * 'give it away' so that the SUID is still forced on.
		 */
		if ( (dvp->v_mount->mnt_flag & MNT_SUIDDIR) &&
		   (dp->i_mode & ISUID) && dp->i_uid) {
			dmode |= ISUID;
			ip->i_uid = dp->i_uid;
#ifdef QUOTA
			if (dp->i_uid != cnp->cn_cred->cr_uid) {
				/*
				 * make sure the correct user gets charged
				 * for the space.
				 * Make a dummy credential for the victim.
				 * XXX This seems to never be accessed out of
				 * our context so a stack variable is ok.
				 */
				ucred.cr_ref = 1;
				ucred.cr_uid = ip->i_uid;
				ucred.cr_ngroups = 1;
				ucred.cr_groups[0] = dp->i_gid;
				ucp = &ucred;
			}
#endif
		} else {
			ip->i_uid = cnp->cn_cred->cr_uid;
		}
#ifdef QUOTA
		if ((error = ext2_getinoquota(ip)) ||
		(error = ext2_chkiq(ip, 1, ucp, 0))) {
			EXT2_VFREE(tvp, ip->i_number, dmode);
			vput(tvp);
			return (error);
		}
#endif
	}
#else
	ip->i_uid = cnp->cn_cred->cr_uid;
#ifdef QUOTA
	if ((error = ext2_getinoquota(ip)) ||
	    (error = ext2_chkiq(ip, 1, cnp->cn_cred, 0))) {
		EXT2_VFREE(tvp, ip->i_number, dmode);
		vput(tvp);
		return (error);
	}
#endif
#endif
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_mode = dmode;
	tvp->v_type = VDIR;	/* Rest init'd in getnewvnode(). */
	ip->i_nlink = 2;
	if (cnp->cn_flags & CNP_ISWHITEOUT)
		ip->i_flags |= UF_OPAQUE;
	error = EXT2_UPDATE(tvp, 1);

	/*
	 * The vnode must have a VM object in order to issue buffer cache
	 * ops on it.
	 */
	vinitvmio(tvp, 0, PAGE_SIZE, -1);

	/*
	 * Bump link count in parent directory
	 * to reflect work done below.  Should
	 * be done before reference is created
	 * so reparation is possible if we crash.
	 */
	dp->i_nlink++;
	dp->i_flag |= IN_CHANGE;
	error = EXT2_UPDATE(dvp, 1);
	if (error)
		goto bad;

	/* Initialize directory with "." and ".." from static template. */
	if (EXT2_HAS_INCOMPAT_FEATURE(ip->i_e2fs->s_es,
	    EXT2_FEATURE_INCOMPAT_FILETYPE))
		dtp = &ext2_mastertemplate;
	else
		dtp = &ext2_omastertemplate;
	dirtemplate = *dtp;
	dirtemplate.dot_ino = ip->i_number;
	dirtemplate.dotdot_ino = dp->i_number;
	/* note that in ext2 DIRBLKSIZ == blocksize, not DEV_BSIZE
	 * so let's just redefine it - for this function only
	 */
#undef  DIRBLKSIZ
#define DIRBLKSIZ  VTOI(dvp)->i_e2fs->s_blocksize
	dirtemplate.dotdot_reclen = DIRBLKSIZ - 12;
	error = vn_rdwr(UIO_WRITE, tvp, (caddr_t)&dirtemplate,
			sizeof (dirtemplate), (off_t)0, UIO_SYSSPACE,
			IO_NODELOCKED|IO_SYNC, cnp->cn_cred, NULL);
	if (error) {
		dp->i_nlink--;
		dp->i_flag |= IN_CHANGE;
		goto bad;
	}
	if (DIRBLKSIZ > VFSTOEXT2(dvp->v_mount)->um_mountp->mnt_stat.f_bsize)
		panic("ext2_mkdir: blksize"); /* XXX should grow with balloc() */
	else {
		ip->i_size = DIRBLKSIZ;
		ip->i_flag |= IN_CHANGE;
	}

	/* Directory set up, now install its entry in the parent directory. */
	error = ext2_direnter(ip, dvp, cnp);
	if (error) {
		dp->i_nlink--;
		dp->i_flag |= IN_CHANGE;
	}
bad:
	/*
	 * No need to do an explicit VOP_TRUNCATE here, vrele will do this
	 * for us because we set the link count to 0.
	 */
	if (error) {
		ip->i_nlink = 0;
		ip->i_flag |= IN_CHANGE;
		vput(tvp);
	} else
		*ap->a_vpp = tvp;
out:
	return (error);
#undef  DIRBLKSIZ
#define DIRBLKSIZ  DEV_BSIZE
}

/*
 * Rmdir system call.
 *
 * ext2_rmdir(struct vnode *a_dvp, struct vnode *a_vp,
 *	      struct componentname *a_cnp)
 */
static int
ext2_rmdir(struct vop_old_rmdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip, *dp;
	int error;

	ip = VTOI(vp);
	dp = VTOI(dvp);

	/*
	 * Verify the directory is empty (and valid).
	 * (Rmdir ".." won't be valid since
	 *  ".." will contain a reference to
	 *  the current directory and thus be
	 *  non-empty.)
	 */
	error = 0;
	if (ip->i_nlink != 2 || !ext2_dirempty(ip, dp->i_number, cnp->cn_cred)) {
		error = ENOTEMPTY;
		goto out;
	}
	if ((dp->i_flags & APPEND)
	    || (ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND))) {
		error = EPERM;
		goto out;
	}
	/*
	 * Delete reference to directory before purging
	 * inode.  If we crash in between, the directory
	 * will be reattached to lost+found,
	 */
	error = ext2_dirremove(dvp, cnp);
	if (error)
		goto out;
	dp->i_nlink--;
	dp->i_flag |= IN_CHANGE;
	vn_unlock(dvp);
	/*
	 * Truncate inode.  The only stuff left
	 * in the directory is "." and "..".  The
	 * "." reference is inconsequential since
	 * we're quashing it.  The ".." reference
	 * has already been adjusted above.  We've
	 * removed the "." reference and the reference
	 * in the parent directory, but there may be
	 * other hard links so decrement by 2 and
	 * worry about them later.
	 */
	ip->i_nlink -= 2;
	error = EXT2_TRUNCATE(vp, (off_t)0, IO_SYNC, cnp->cn_cred);
	vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
out:
	return (error);
}

/*
 * symlink -- make a symbolic link
 *
 * ext2_symlink(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnp, struct vattr *a_vap,
 *		char *a_target)
 */
static int
ext2_symlink(struct vop_old_symlink_args *ap)
{
	struct vnode *vp, **vpp = ap->a_vpp;
	struct inode *ip;
	int len, error;

	error = ext2_makeinode(IFLNK | ap->a_vap->va_mode, ap->a_dvp,
	    vpp, ap->a_cnp);
	if (error)
		return (error);
	vp = *vpp;
	len = strlen(ap->a_target);
	if (len < vp->v_mount->mnt_maxsymlinklen) {
		ip = VTOI(vp);
		bcopy(ap->a_target, (char *)ip->i_shortlink, len);
		ip->i_size = len;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	} else {
		/*
		 * Make sure we have a VM object in order to use
		 * the buffer cache.
		 */
		if (vp->v_object == NULL)
			vinitvmio(vp, 0, PAGE_SIZE, -1);

		error = vn_rdwr(UIO_WRITE, vp, ap->a_target, len, (off_t)0,
				UIO_SYSSPACE, IO_NODELOCKED,
				ap->a_cnp->cn_cred, NULL);

		if (error)
			vput(vp);
	}

	return (error);
}

/*
 * Allocate a new inode.
 */
static int
ext2_makeinode(int mode, struct vnode *dvp, struct vnode **vpp,
	       struct componentname *cnp)
{
	struct inode *ip, *pdir;
	struct vnode *tvp;
	int error;

	pdir = VTOI(dvp);
	*vpp = NULL;
	if ((mode & IFMT) == 0)
		mode |= IFREG;

	error = EXT2_VALLOC(dvp, mode, cnp->cn_cred, &tvp);
	if (error) {
		return (error);
	}
	ip = VTOI(tvp);
	ip->i_gid = pdir->i_gid;
#ifdef SUIDDIR
	{
#ifdef QUOTA
		struct ucred ucred, *ucp;
		ucp = cnp->cn_cred;
#endif
		/*
		 * if we are
		 * not the owner of the directory,
		 * and we are hacking owners here, (only do this where told to)
		 * and we are not giving it TOO root, (would subvert quotas)
		 * then go ahead and give it to the other user.
		 * Note that this drops off the execute bits for security.
		 */
		if ( (dvp->v_mount->mnt_flag & MNT_SUIDDIR) &&
		     (pdir->i_mode & ISUID) &&
		     (pdir->i_uid != cnp->cn_cred->cr_uid) && pdir->i_uid) {
			ip->i_uid = pdir->i_uid;
			mode &= ~07111;
#ifdef QUOTA
			/*
			 * make sure the correct user gets charged
			 * for the space.
			 * Quickly knock up a dummy credential for the victim.
			 * XXX This seems to never be accessed out of our
			 * context so a stack variable is ok.
			 */
			ucred.cr_ref = 1;
			ucred.cr_uid = ip->i_uid;
			ucred.cr_ngroups = 1;
			ucred.cr_groups[0] = pdir->i_gid;
			ucp = &ucred;
#endif
		} else {
			ip->i_uid = cnp->cn_cred->cr_uid;
		}

#ifdef QUOTA
		if ((error = ext2_getinoquota(ip)) ||
		(error = ext2_chkiq(ip, 1, ucp, 0))) {
			EXT2_VFREE(tvp, ip->i_number, mode);
			vput(tvp);
			return (error);
		}
#endif
	}
#else
	ip->i_uid = cnp->cn_cred->cr_uid;
#ifdef QUOTA
	if ((error = ext2_getinoquota(ip)) ||
	    (error = ext2_chkiq(ip, 1, cnp->cn_cred, 0))) {
		EXT2_VFREE(tvp, ip->i_number, mode);
		vput(tvp);
		return (error);
	}
#endif
#endif
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_mode = mode;
	tvp->v_type = IFTOVT(mode);	/* Rest init'd in getnewvnode(). */
	ip->i_nlink = 1;
	if ((ip->i_mode & ISGID) && !groupmember(ip->i_gid, cnp->cn_cred) &&
	    priv_check_cred(cnp->cn_cred, PRIV_VFS_SETGID, 0))
		ip->i_mode &= ~ISGID;

	if (cnp->cn_flags & CNP_ISWHITEOUT)
		ip->i_flags |= UF_OPAQUE;

	/*
	 * Regular files and directories need VM objects.  Softlinks do
	 * not (not immediately anyway).
	 */
	if (tvp->v_type == VREG || tvp->v_type == VDIR)
		vinitvmio(tvp, 0, PAGE_SIZE, -1);

	/*
	 * Make sure inode goes to disk before directory entry.
	 */
	error = EXT2_UPDATE(tvp, 1);
	if (error)
		goto bad;
	error = ext2_direnter(ip, dvp, cnp);
	if (error)
		goto bad;

	*vpp = tvp;
	return (0);

bad:
	/*
	 * Write error occurred trying to update the inode
	 * or the directory so must deallocate the inode.
	 */
	ip->i_nlink = 0;
	ip->i_flag |= IN_CHANGE;
	vput(tvp);
	return (error);
}

void
ext2_itimes(struct vnode *vp)
{
	struct inode *ip;
	struct timespec ts;

	ip = VTOI(vp);
	if ((ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_UPDATE)) == 0)
		return;
	if ((vp->v_type == VBLK || vp->v_type == VCHR) && !DOINGSOFTDEP(vp))
		ip->i_flag |= IN_LAZYMOD;
	else
		ip->i_flag |= IN_MODIFIED;
	if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
		vfs_timestamp(&ts);
		if (ip->i_flag & IN_ACCESS) {
			ip->i_atime = ts.tv_sec;
			ip->i_atimensec = ts.tv_nsec;
		}
		if (ip->i_flag & IN_UPDATE) {
			ip->i_mtime = ts.tv_sec;
			ip->i_mtimensec = ts.tv_nsec;
			ip->i_modrev++;
		}
		if (ip->i_flag & IN_CHANGE) {
			ip->i_ctime = ts.tv_sec;
			ip->i_ctimensec = ts.tv_nsec;
		}
	}
	ip->i_flag &= ~(IN_ACCESS | IN_CHANGE | IN_UPDATE);
}

/*
 * Open called.
 *
 * Nothing to do.
 *
 * ext2_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	     struct file *a_fp)
 */
/* ARGSUSED */
static
int
ext2_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;

	/*
	 * Files marked append-only must be opened for appending.
	 */
	if ((VTOI(vp)->i_flags & APPEND) &&
	    (ap->a_mode & (FWRITE | O_APPEND)) == FWRITE) {
		return (EPERM);
	}
	return (vop_stdopen(ap));
}

/*
 * Close called.
 *
 * Update the times on the inode.
 *
 * ext2_close(struct vnode *a_vp, int a_fflag, struct ucred *a_cred)
 */
/* ARGSUSED */
static
int
ext2_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;

	if (VREFCNT(vp) > 1)
		ext2_itimes(vp);
	return (vop_stdclose(ap));
}

/*
 * ext2_access(struct vnode *a_vp, int a_mode, struct ucred *a_cred)
 */
static
int
ext2_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	int error;

#ifdef QUOTA
	if (ap->a_mode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if ((error = ext2_getinoquota(ip)) != 0)
				return (error);
			break;
		default:
			break;
		}
	}
#endif

	error = vop_helper_access(ap, ip->i_uid, ip->i_gid, ip->i_mode,
				  ip->i_flags);
	return (error);
}

/*
 * ext2_getattr(struct vnode *a_vp, struct vattr *a_vap)
 */
/* ARGSUSED */
static
int
ext2_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct vattr *vap = ap->a_vap;

	ext2_itimes(vp);
	/*
	 * Copy from inode table
	 */
	vap->va_fsid = dev2udev(ip->i_dev);
	vap->va_fileid = ip->i_number;
	vap->va_mode = ip->i_mode & ~IFMT;
	vap->va_nlink = VFSTOEXT2(vp->v_mount)->um_i_effnlink_valid ?
	    ip->i_effnlink : ip->i_nlink;
	vap->va_uid = ip->i_uid;
	vap->va_gid = ip->i_gid;
	vap->va_rdev = makedev( umajor(ip->i_rdev), uminor(ip->i_rdev) );
	vap->va_size = ip->i_din.di_size;
	vap->va_atime.tv_sec = ip->i_atime;
	vap->va_atime.tv_nsec = ip->i_atimensec;
	vap->va_mtime.tv_sec = ip->i_mtime;
	vap->va_mtime.tv_nsec = ip->i_mtimensec;
	vap->va_ctime.tv_sec = ip->i_ctime;
	vap->va_ctime.tv_nsec = ip->i_ctimensec;
	vap->va_flags = ip->i_flags;
	vap->va_gen = ip->i_gen;
	vap->va_blocksize = vp->v_mount->mnt_stat.f_iosize;
	vap->va_bytes = dbtob((u_quad_t)ip->i_blocks);
	vap->va_type = IFTOVT(ip->i_mode);
	vap->va_filerev = ip->i_modrev;
	return (0);
}

/*
 * Set attribute vnode op. called from several syscalls
 *
 * ext2_setattr(struct vnode *a_vp, struct vattr *a_vap, struct ucred *a_cred)
 */
static
int
ext2_setattr(struct vop_setattr_args *ap)
{
	struct vattr *vap = ap->a_vap;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct ucred *cred = ap->a_cred;
	int error;

	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (major(vap->va_rdev) != VNOVAL) ||
	    ((int)vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		return (EINVAL);
	}
	if (vap->va_flags != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != ip->i_uid &&
		    (error = priv_check_cred(cred, PRIV_VFS_SETATTR, 0)))
			return (error);
		/*
		 * Note that a root chflags becomes a user chflags when
		 * we are jailed, unless the jail.chflags_allowed sysctl
		 * is set.
		 */
		if (cred->cr_uid == 0 &&
		    (!jailed(cred) || jail_chflags_allowed)) {
			if ((ip->i_flags
			    & (SF_NOUNLINK | SF_IMMUTABLE | SF_APPEND)) &&
			    securelevel > 0)
				return (EPERM);
			ip->i_flags = vap->va_flags;
		} else {
			if (ip->i_flags
			    & (SF_NOUNLINK | SF_IMMUTABLE | SF_APPEND) ||
			    (vap->va_flags & UF_SETTABLE) != vap->va_flags)
				return (EPERM);
			ip->i_flags &= SF_SETTABLE;
			ip->i_flags |= (vap->va_flags & UF_SETTABLE);
		}
		ip->i_flag |= IN_CHANGE;
		if (vap->va_flags & (IMMUTABLE | APPEND))
			return (0);
	}
	if (ip->i_flags & (IMMUTABLE | APPEND))
		return (EPERM);
	/*
	 * Go through the fields and update iff not VNOVAL.
	 */
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if ((error = ext2_chown(vp, vap->va_uid, vap->va_gid, cred)) != 0)
			return (error);
	}
	if (vap->va_size != VNOVAL) {
		/*
		 * Disallow write attempts on read-only filesystems;
		 * unless the file is a socket, fifo, or a block or
		 * character device resident on the filesystem.
		 */
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			break;
		}
		if ((error = EXT2_TRUNCATE(vp, vap->va_size, 0, cred)) != 0)
			return (error);
	}
	ip = VTOI(vp);
	if (vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != ip->i_uid &&
		    (error = priv_check_cred(cred, PRIV_VFS_SETATTR, 0)) &&
		    ((vap->va_vaflags & VA_UTIMES_NULL) == 0 ||
		    (error = VOP_EACCESS(vp, VWRITE, cred))))
			return (error);
		if (vap->va_atime.tv_sec != VNOVAL)
			ip->i_flag |= IN_ACCESS;
		if (vap->va_mtime.tv_sec != VNOVAL)
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		ext2_itimes(vp);
		if (vap->va_atime.tv_sec != VNOVAL) {
			ip->i_atime = vap->va_atime.tv_sec;
			ip->i_atimensec = vap->va_atime.tv_nsec;
		}
		if (vap->va_mtime.tv_sec != VNOVAL) {
			ip->i_mtime = vap->va_mtime.tv_sec;
			ip->i_mtimensec = vap->va_mtime.tv_nsec;
		}
		error = EXT2_UPDATE(vp, 0);
		if (error)
			return (error);
	}
	error = 0;
	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		error = ext2_chmod(vp, (int)vap->va_mode, cred);
	}
	VN_KNOTE(vp, NOTE_ATTRIB);
	return (error);
}

/*
 * Change the mode on a file.
 * Inode must be locked before calling.
 */
static int
ext2_chmod(struct vnode *vp, int mode, struct ucred *cred)
{
	struct inode *ip = VTOI(vp);
	int error;

	if (cred->cr_uid != ip->i_uid) {
	    error = priv_check_cred(cred, PRIV_VFS_CHMOD, 0);
	    if (error)
		return (error);
	}
	if (cred->cr_uid) {
		if (vp->v_type != VDIR && (mode & S_ISTXT))
			return (EFTYPE);
		if (!groupmember(ip->i_gid, cred) && (mode & ISGID))
			return (EPERM);
	}
	ip->i_mode &= ~ALLPERMS;
	ip->i_mode |= (mode & ALLPERMS);
	ip->i_flag |= IN_CHANGE;
	return (0);
}

/*
 * Perform chown operation on inode ip;
 * inode must be locked prior to call.
 */
static int
ext2_chown(struct vnode *vp, uid_t uid, gid_t gid, struct ucred *cred)
{
	struct inode *ip = VTOI(vp);
	uid_t ouid;
	gid_t ogid;
	int error = 0;
#ifdef QUOTA
	int i;
	long change;
#endif

	if (uid == (uid_t)VNOVAL)
		uid = ip->i_uid;
	if (gid == (gid_t)VNOVAL)
		gid = ip->i_gid;
	/*
	 * If we don't own the file, are trying to change the owner
	 * of the file, or are not a member of the target group,
	 * the caller must be superuser or the call fails.
	 */
	if ((cred->cr_uid != ip->i_uid || uid != ip->i_uid ||
	    (gid != ip->i_gid && !(cred->cr_gid == gid ||
	    groupmember(gid, cred)))) &&
	    (error = priv_check_cred(cred, PRIV_VFS_CHOWN, 0)))
		return (error);
	ogid = ip->i_gid;
	ouid = ip->i_uid;
#ifdef QUOTA
	if ((error = ext2_getinoquota(ip)) != 0)
		return (error);
	if (ouid == uid) {
		ext2_dqrele(vp, ip->i_dquot[USRQUOTA]);
		ip->i_dquot[USRQUOTA] = NODQUOT;
	}
	if (ogid == gid) {
		ext2_dqrele(vp, ip->i_dquot[GRPQUOTA]);
		ip->i_dquot[GRPQUOTA] = NODQUOT;
	}
	change = ip->i_blocks;
	(void) ext2_chkdq(ip, -change, cred, CHOWN);
	(void) ext2_chkiq(ip, -1, cred, CHOWN);
	for (i = 0; i < MAXQUOTAS; i++) {
		ext2_dqrele(vp, ip->i_dquot[i]);
		ip->i_dquot[i] = NODQUOT;
	}
#endif
	ip->i_gid = gid;
	ip->i_uid = uid;
#ifdef QUOTA
	if ((error = ext2_getinoquota(ip)) == 0) {
		if (ouid == uid) {
			ext2_dqrele(vp, ip->i_dquot[USRQUOTA]);
			ip->i_dquot[USRQUOTA] = NODQUOT;
		}
		if (ogid == gid) {
			ext2_dqrele(vp, ip->i_dquot[GRPQUOTA]);
			ip->i_dquot[GRPQUOTA] = NODQUOT;
		}
		if ((error = ext2_chkdq(ip, change, cred, CHOWN)) == 0) {
			if ((error = ext2_chkiq(ip, 1, cred, CHOWN)) == 0)
				goto good;
			else
				(void)ext2_chkdq(ip, -change, cred, CHOWN|FORCE);
		}
		for (i = 0; i < MAXQUOTAS; i++) {
			ext2_dqrele(vp, ip->i_dquot[i]);
			ip->i_dquot[i] = NODQUOT;
		}
	}
	ip->i_gid = ogid;
	ip->i_uid = ouid;
	if (ext2_getinoquota(ip) == 0) {
		if (ouid == uid) {
			ext2_dqrele(vp, ip->i_dquot[USRQUOTA]);
			ip->i_dquot[USRQUOTA] = NODQUOT;
		}
		if (ogid == gid) {
			ext2_dqrele(vp, ip->i_dquot[GRPQUOTA]);
			ip->i_dquot[GRPQUOTA] = NODQUOT;
		}
		(void) ext2_chkdq(ip, change, cred, FORCE|CHOWN);
		(void) ext2_chkiq(ip, 1, cred, FORCE|CHOWN);
		(void) ext2_getinoquota(ip);
	}
	return (error);
good:
	if (ext2_getinoquota(ip))
		panic("ext2_chown: lost quota");
#endif /* QUOTA */
	ip->i_flag |= IN_CHANGE;
	if (cred->cr_uid != 0 && (ouid != uid || ogid != gid))
		ip->i_mode &= ~(ISUID | ISGID);
	return (0);
}

/*
 * Mmap a file
 *
 * NB Currently unsupported.
 *
 * ext2_mmap(struct vnode *a_vp, int a_fflags, struct ucred *a_cred)
 */
/* ARGSUSED */
static
int
ext2_mmap(struct vop_mmap_args *ap)
{
	return (EINVAL);
}

/*
 * whiteout vnode call
 *
 * ext2_whiteout(struct vnode *a_dvp, struct componentname *a_cnp, int a_flags)
 */
static
int
ext2_whiteout(struct vop_old_whiteout_args *ap)
{
	return (EOPNOTSUPP);
}

/*
 * Return target name of a symbolic link
 *
 * ext2_readlink(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred)
 */
static
int
ext2_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	int isize;

	isize = ip->i_size;
	if ((isize < vp->v_mount->mnt_maxsymlinklen) ||
	    (ip->i_din.di_blocks == 0)) {   /* XXX - for old fastlink support */
		uiomove((char *)ip->i_shortlink, isize, ap->a_uio);
		return (0);
	}
	return (VOP_READ(vp, ap->a_uio, 0, ap->a_cred));
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 *
 * In order to be able to swap to a file, the VOP_BMAP operation may not
 * deadlock on memory.  See ext2_bmap() for details.
 *
 * ext2_strategy(struct vnode *a_vp, struct bio *a_bio)
 */
static
int
ext2_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct bio *nbio;
	struct buf *bp = bio->bio_buf;
	struct vnode *vp = ap->a_vp;
	struct inode *ip;
	int error;

	ip = VTOI(vp);
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		panic("ext2_strategy: spec");
	nbio = push_bio(bio);
	if (nbio->bio_offset == NOOFFSET) {
		error = VOP_BMAP(vp, bio->bio_offset, &nbio->bio_offset,
				 NULL, NULL, bp->b_cmd);
		if (error) {
			bp->b_error = error;
			bp->b_flags |= B_ERROR;
			/* I/O was never started on nbio, must biodone(bio) */
			biodone(bio);
			return (error);
		}
		if (nbio->bio_offset == NOOFFSET)
			vfs_bio_clrbuf(bp);
	}
	if (nbio->bio_offset == NOOFFSET) {
		/* I/O was never started on nbio, must biodone(bio) */
		biodone(bio);
		return (0);
	}
	vn_strategy(ip->i_devvp, nbio);
	return (0);
}

/*
 * Print out the contents of an inode.
 *
 * ext2_print(struct vnode *a_vp)
 */
static
int
ext2_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);

	kprintf("tag VT_EXT2FS, ino %lu, on dev %s (%d, %d)",
	    (u_long)ip->i_number, devtoname(ip->i_dev), major(ip->i_dev),
	    minor(ip->i_dev));
	if (vp->v_type == VFIFO)
		fifo_printinfo(vp);
	lockmgr_printinfo(&vp->v_lock);
	kprintf("\n");
	return (0);
}

/*
 * Read wrapper for fifos.
 *
 * ext2fifo_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *		struct ucred *a_cred)
 */
static
int
ext2fifo_read(struct vop_read_args *ap)
{
	int error, resid;
	struct inode *ip;
	struct uio *uio;

	uio = ap->a_uio;
	resid = uio->uio_resid;
	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	ip = VTOI(ap->a_vp);
	if ((ap->a_vp->v_mount->mnt_flag & MNT_NOATIME) == 0 && ip != NULL &&
	    (uio->uio_resid != resid || (error == 0 && resid != 0)))
		VTOI(ap->a_vp)->i_flag |= IN_ACCESS;
	return (error);
}

/*
 * Write wrapper for fifos.
 *
 * ext2fifo_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *		 struct ucred *a_cred)
 */
static
int
ext2fifo_write(struct vop_write_args *ap)
{
	int error, resid;
	struct inode *ip;
	struct uio *uio;

	uio = ap->a_uio;
	resid = uio->uio_resid;
	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	ip = VTOI(ap->a_vp);
	if (ip != NULL && (uio->uio_resid != resid || (error == 0 && resid != 0)))
		VTOI(ap->a_vp)->i_flag |= IN_CHANGE | IN_UPDATE;
	return (error);
}

/*
 * Close wrapper for fifos.
 *
 * Update the times on the inode then do device close.
 *
 * ext2fifo_close(struct vnode *a_vp, int a_fflag, struct ucred *a_cred)
 */
static
int
ext2fifo_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;

	if (VREFCNT(vp) > 1)
		ext2_itimes(vp);
	return (VOCALL(&fifo_vnode_vops, &ap->a_head));
}

/*
 * Kqfilter wrapper for fifos.
 *
 * Fall through to ext2 kqfilter routines if needed
 */
static
int
ext2fifo_kqfilter(struct vop_kqfilter_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	if (error)
		error = ext2_kqfilter(ap);
	return (error);
}

/*
 * Return POSIX pathconf information applicable to ext2 filesystems.
 *
 * ext2_pathconf(struct vnode *a_vp, int a_name, int *a_retval)
 */
static
int
ext2_pathconf(struct vop_pathconf_args *ap)
{
	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		return (0);
	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		return (0);
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		return (0);
	case _PC_PIPE_BUF:
		*ap->a_retval = PIPE_BUF;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		return (0);
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}

/*
 * Advisory record locking support
 *
 * ext2_advlock(struct vnode *a_vp, caddr_t a_id, int a_op, struct flock *a_fl,
 *	       int a_flags)
 */
static
int
ext2_advlock(struct vop_advlock_args *ap)
{
	struct inode *ip = VTOI(ap->a_vp);

	return (lf_advlock(ap, &(ip->i_lockf), ip->i_size));
}

/*
 * Initialize the vnode associated with a new inode, handle aliased
 * vnodes.
 */
int
ext2_vinit(struct mount *mntp, struct vnode **vpp)
{
	struct inode *ip;
	struct vnode *vp;
	struct timeval tv;

	vp = *vpp;
	ip = VTOI(vp);

	switch(vp->v_type = IFTOVT(ip->i_mode)) {
	case VCHR:
	case VBLK:
		vp->v_ops = &mntp->mnt_vn_spec_ops;
		addaliasu(vp, umajor(ip->i_rdev), uminor(ip->i_rdev));
		break;
	case VFIFO:
		vp->v_ops = &mntp->mnt_vn_fifo_ops;
		break;
	case VDIR:
	case VREG:
		vinitvmio(vp, ip->i_size, PAGE_SIZE, -1); /* XXX */
		break;
	case VLNK:
		if ((ip->i_size >= vp->v_mount->mnt_maxsymlinklen) &&
		    ip->i_din.di_blocks != 0
		) {
			vinitvmio(vp, ip->i_size, PAGE_SIZE, -1);
		}
		break;
	default:
		break;

	}

	if (ip->i_number == ROOTINO)
		vp->v_flag |= VROOT;
	/*
	 * Initialize modrev times
	 */
	getmicrouptime(&tv);
	SETHIGH(ip->i_modrev, tv.tv_sec);
	SETLOW(ip->i_modrev, tv.tv_usec * 4294);
	*vpp = vp;
	return (0);
}

static struct filterops ext2read_filtops =
	{ FILTEROP_ISFD, NULL, filt_ext2detach, filt_ext2read };
static struct filterops ext2write_filtops =
	{ FILTEROP_ISFD, NULL, filt_ext2detach, filt_ext2write };
static struct filterops ext2vnode_filtops =
	{ FILTEROP_ISFD, NULL, filt_ext2detach, filt_ext2vnode };

/*
 * ext2_kqfilter(struct vnode *a_vp, struct knote *a_kn)
 */
static int
ext2_kqfilter(struct vop_kqfilter_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &ext2read_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &ext2write_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &ext2vnode_filtops;
		break;
	default:
		return (EOPNOTSUPP);
	}

	kn->kn_hook = (caddr_t)vp;

	/* XXX: kq token actually protects the list */
	lwkt_gettoken(&vp->v_token);
	knote_insert(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
	lwkt_reltoken(&vp->v_token);

	return (0);
}

static void
filt_ext2detach(struct knote *kn)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;

	lwkt_gettoken(&vp->v_token);
	knote_remove(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
	lwkt_reltoken(&vp->v_token);
}

/*ARGSUSED*/
static int
filt_ext2read(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;
	struct inode *ip = VTOI(vp);
	off_t off;

	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);
		return (1);
	}
        off = ip->i_size - kn->kn_fp->f_offset;
	kn->kn_data = (off < INTPTR_MAX) ? off : INTPTR_MAX;
	if (kn->kn_sfflags & NOTE_OLDAPI)
		return(1);
        return (kn->kn_data != 0);
}

/*ARGSUSED*/
static int
filt_ext2write(struct knote *kn, long hint)
{
	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE)
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);

        kn->kn_data = 0;
        return (1);
}

static int
filt_ext2vnode(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		return (1);
	}
	return (kn->kn_fflags != 0);
}

struct vop_ops ext2_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		ext2_fsync,
	.vop_read =		ext2_read,
	.vop_reallocblks =	ext2_reallocblks,
	.vop_write =		ext2_write,
	.vop_access =		ext2_access,
	.vop_advlock =		ext2_advlock,
	.vop_bmap =		ext2_bmap,
	.vop_old_lookup =	ext2_lookup,
	.vop_close =		ext2_close,
	.vop_old_create =	ext2_create,
	.vop_getattr =		ext2_getattr,
	.vop_inactive =		ext2_inactive,
	.vop_old_link =		ext2_link,
	.vop_old_mkdir =	ext2_mkdir,
	.vop_old_mknod =	ext2_mknod,
	.vop_mmap =		ext2_mmap,
	.vop_open =		ext2_open,
	.vop_pathconf =		ext2_pathconf,
	.vop_kqfilter =		ext2_kqfilter,
	.vop_print =		ext2_print,
	.vop_readdir =		ext2_readdir,
	.vop_readlink =		ext2_readlink,
	.vop_reclaim =		ext2_reclaim,
	.vop_old_remove =	ext2_remove,
	.vop_old_rename =	ext2_rename,
	.vop_old_rmdir =	ext2_rmdir,
	.vop_setattr =		ext2_setattr,
	.vop_strategy =		ext2_strategy,
	.vop_old_symlink =	ext2_symlink,
	.vop_old_whiteout =	ext2_whiteout,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages
};

struct vop_ops ext2_spec_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		ext2_fsync,
	.vop_access =		ext2_access,
	.vop_close =		ext2_close,
	.vop_getattr =		ext2_getattr,
	.vop_inactive =		ext2_inactive,
	.vop_print =		ext2_print,
	.vop_read =		vop_stdnoread,
	.vop_reclaim =		ext2_reclaim,
	.vop_setattr =		ext2_setattr,
	.vop_write =		vop_stdnowrite
};

struct vop_ops ext2_fifo_vops = {
	.vop_default =		ext2_vnoperatefifo,
	.vop_fsync =		ext2_fsync,
	.vop_access =		ext2_access,
	.vop_close =		ext2fifo_close,
	.vop_getattr =		ext2_getattr,
	.vop_inactive =		ext2_inactive,
	.vop_kqfilter =		ext2fifo_kqfilter,
	.vop_print =		ext2_print,
	.vop_read =		ext2fifo_read,
	.vop_reclaim =		ext2_reclaim,
	.vop_setattr =		ext2_setattr,
	.vop_write =		ext2fifo_write
};

VNODEOP_SET(ext2_vnode_vops);
VNODEOP_SET(ext2_spec_vops);
VNODEOP_SET(ext2_fifo_vops);

/*
 * ext2_vnoperate()
 */
int
ext2_vnoperate(struct vop_generic_args *ap)
{
	return (VOCALL(&ext2_vnode_vops, ap));
}

/*
 * ext2_vnoperatefifo()
 */
int
ext2_vnoperatefifo(struct vop_generic_args *ap)
{
	return (VOCALL(&ext2_fifo_vops, ap));
}
