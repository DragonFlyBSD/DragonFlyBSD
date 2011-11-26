/*
 * (The copyright below applies to ufs_access())
 *
 * Copyright (c) 1982, 1986, 1989, 1993, 1995
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
 * @(#)ufs_vnops.c	8.27 (Berkeley) 5/27/95
 * $DragonFly: src/sys/kern/vfs_helper.c,v 1.5 2008/05/25 18:34:46 dillon Exp $
 */

#include "opt_quota.h"
#include "opt_suiddir.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/file.h>		/* XXX */
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/jail.h>

/*
 * vop_helper_access()
 *
 *	Provide standard UNIX semanics for VOP_ACCESS, but without the quota
 *	code.  This procedure was basically pulled out of UFS.
 */
int
vop_helper_access(struct vop_access_args *ap, uid_t ino_uid, gid_t ino_gid,
		  mode_t ino_mode, u_int32_t ino_flags)
{
	struct vnode *vp = ap->a_vp;
	struct ucred *cred = ap->a_cred;
	mode_t mask, mode = ap->a_mode;
	gid_t *gp;
	int i;
	uid_t proc_uid;
	gid_t proc_gid;

	if (ap->a_flags & AT_EACCESS) {
		proc_uid = cred->cr_uid;
		proc_gid = cred->cr_gid;
	} else {
		proc_uid = cred->cr_ruid;
		proc_gid = cred->cr_rgid;
	}

	/*
	 * Disallow write attempts on read-only filesystems;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the filesystem.
	 */
	if (mode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
		case VDATABASE:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			break;
		}
	}

	/* If immutable bit set, nobody gets to write it. */
	if ((mode & VWRITE) && (ino_flags & IMMUTABLE))
		return (EPERM);

	/* Otherwise, user id 0 always gets access. */
	if (proc_uid == 0)
		return (0);

	mask = 0;

	/* Otherwise, check the owner. */
	if (proc_uid == ino_uid) {
		if (mode & VEXEC)
			mask |= S_IXUSR;
		if (mode & VREAD)
			mask |= S_IRUSR;
		if (mode & VWRITE)
			mask |= S_IWUSR;
		return ((ino_mode & mask) == mask ? 0 : EACCES);
	}

	/* 
	 * Otherwise, check the groups. 
	 * We must special-case the primary group to, if needed, check against
	 * the real gid and not the effective one.
	 */
	if (proc_gid == ino_gid) {
		if (mode & VEXEC)
			mask |= S_IXGRP;
		if (mode & VREAD)
			mask |= S_IRGRP;
		if (mode & VWRITE)
			mask |= S_IWGRP;
		return ((ino_mode & mask) == mask ? 0 : EACCES);
	}
	for (i = 1, gp = &cred->cr_groups[1]; i < cred->cr_ngroups; i++, gp++)
		if (ino_gid == *gp) {
			if (mode & VEXEC)
				mask |= S_IXGRP;
			if (mode & VREAD)
				mask |= S_IRGRP;
			if (mode & VWRITE)
				mask |= S_IWGRP;
			return ((ino_mode & mask) == mask ? 0 : EACCES);
		}

	/* Otherwise, check everyone else. */
	if (mode & VEXEC)
		mask |= S_IXOTH;
	if (mode & VREAD)
		mask |= S_IROTH;
	if (mode & VWRITE)
		mask |= S_IWOTH;
	return ((ino_mode & mask) == mask ? 0 : EACCES);
}

int
vop_helper_setattr_flags(u_int32_t *ino_flags, u_int32_t vaflags,
			 uid_t uid, struct ucred *cred)
{
	int error;

	/*
	 * If uid doesn't match only a privileged user can change the flags
	 */
	if (cred->cr_uid != uid &&
	    (error = priv_check_cred(cred, PRIV_VFS_SYSFLAGS, 0))) {
		return(error);
	}
	if (cred->cr_uid == 0 &&
	    (!jailed(cred)|| jail_chflags_allowed)) {
		if ((*ino_flags & (SF_NOUNLINK|SF_IMMUTABLE|SF_APPEND)) &&
		    securelevel > 0)
			return (EPERM);
		*ino_flags = vaflags;
	} else {
		if (*ino_flags & (SF_NOUNLINK|SF_IMMUTABLE|SF_APPEND) ||
		    (vaflags & UF_SETTABLE) != vaflags)
			return (EPERM);
		*ino_flags &= SF_SETTABLE;
		*ino_flags |= vaflags & UF_SETTABLE;
	}
	return(0);
}

/*
 * This helper function may be used by VFSs to implement UNIX initial
 * ownership semantics when creating new objects inside directories.
 */
uid_t
vop_helper_create_uid(struct mount *mp, mode_t dmode, uid_t duid,
		      struct ucred *cred, mode_t *modep)
{
#ifdef SUIDDIR
	if ((mp->mnt_flag & MNT_SUIDDIR) && (dmode & S_ISUID) &&
	    duid != cred->cr_uid && duid) {
		*modep &= ~07111;
		return(duid);
	}
#endif
	return(cred->cr_uid);
}

/*
 * This helper may be used by VFSs to implement unix chmod semantics.
 */
int
vop_helper_chmod(struct vnode *vp, mode_t new_mode, struct ucred *cred,
		 uid_t cur_uid, gid_t cur_gid, mode_t *cur_modep)
{
	int error;

	if (cred->cr_uid != cur_uid) {
		error = priv_check_cred(cred, PRIV_VFS_CHMOD, 0);
		if (error)
			return (error);
	}
	if (cred->cr_uid) {
		if (vp->v_type != VDIR && (*cur_modep & S_ISTXT))
			return (EFTYPE);
		if (!groupmember(cur_gid, cred) && (*cur_modep & S_ISGID))
			return (EPERM);
	}
	*cur_modep &= ~ALLPERMS;
	*cur_modep |= new_mode & ALLPERMS;
	return(0);
}

/*
 * This helper may be used by VFSs to implement unix chown semantics.
 */
int
vop_helper_chown(struct vnode *vp, uid_t new_uid, gid_t new_gid,
		 struct ucred *cred,
		 uid_t *cur_uidp, gid_t *cur_gidp, mode_t *cur_modep)
{
	gid_t ogid;
	uid_t ouid;
	int error;

	if (new_uid == (uid_t)VNOVAL)
		new_uid = *cur_uidp;
	if (new_gid == (gid_t)VNOVAL)
		new_gid = *cur_gidp;

	/*
	 * If we don't own the file, are trying to change the owner
	 * of the file, or are not a member of the target group,
	 * the caller must be privileged or the call fails.
	 */
	if ((cred->cr_uid != *cur_uidp || new_uid != *cur_uidp ||
	    (new_gid != *cur_gidp && !(cred->cr_gid == new_gid ||
	    groupmember(new_gid, cred)))) &&
	    (error = priv_check_cred(cred, PRIV_VFS_CHOWN, 0))) {
		return (error);
	}
	ogid = *cur_gidp;
	ouid = *cur_uidp;
	/* XXX QUOTA CODE */
	*cur_uidp = new_uid;
	*cur_gidp = new_gid;
	/* XXX QUOTA CODE */

	/*
	 * DragonFly clears both SUID and SGID if either the owner or
	 * group is changed and root isn't doing it.  If root is doing
	 * it we do not clear SUID/SGID.
	 */
	if (cred->cr_uid != 0 && (ouid != new_uid || ogid != new_gid))
		*cur_modep &= ~(S_ISUID | S_ISGID);
	return(0);
}

