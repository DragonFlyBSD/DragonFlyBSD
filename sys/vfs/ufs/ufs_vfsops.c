/*
 * Copyright (c) 1991, 1993, 1994
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
 *	@(#)ufs_vfsops.c	8.8 (Berkeley) 5/20/95
 * $FreeBSD: src/sys/ufs/ufs/ufs_vfsops.c,v 1.17.2.3 2001/10/14 19:08:16 iedowse Exp $
 * $DragonFly: src/sys/vfs/ufs/ufs_vfsops.c,v 1.17 2008/09/17 21:44:25 dillon Exp $
 */

#include "opt_quota.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/malloc.h>
#include <sys/vnode.h>

#include "quota.h"
#include "inode.h"
#include "ufsmount.h"
#include "ufs_extern.h"

MALLOC_DEFINE(M_UFSMNT, "UFS mount", "UFS mount structure");
/*
 * Return the root of a filesystem.
 */
int
ufs_root(struct mount *mp, struct vnode **vpp)
{
	struct vnode *nvp;
	int error;

	error = VFS_VGET(mp, NULL, (ino_t)ROOTINO, &nvp);
	if (error)
		return (error);
	*vpp = nvp;
	return (0);
}

/*
 * Do operations associated with quotas
 */
int
ufs_quotactl(struct mount *mp, int cmds, uid_t uid, caddr_t arg,
	     struct ucred *cred)
{
#ifndef QUOTA
	return (EOPNOTSUPP);
#else
	int cmd, type, error;

	type = cmds & SUBCMDMASK;
	cmd = cmds >> SUBCMDSHIFT;

	if (uid == -1) {
		switch(type) {
			case USRQUOTA:
				uid = cred->cr_ruid;
				break;
			case GRPQUOTA:
				uid = cred->cr_rgid;
				break;
			default:
				return (EINVAL);
		}
	}
					
	/*
	 * Check permissions.
	 */
	switch (cmd) {

	case Q_QUOTAON:
		error = priv_check_cred(cred, PRIV_UFS_QUOTAON, 0);
		break;

	case Q_QUOTAOFF:
		error = priv_check_cred(cred, PRIV_UFS_QUOTAOFF, 0);
		break;

	case Q_SETQUOTA:
		error = priv_check_cred(cred, PRIV_VFS_SETQUOTA, 0);
		break;

	case Q_SETUSE:
		error = priv_check_cred(cred, PRIV_UFS_SETUSE, 0);
		break;

	case Q_GETQUOTA:
		if (uid == cred->cr_ruid)
			error = 0;
		else
			error = priv_check_cred(cred, PRIV_VFS_GETQUOTA, 0);
		break;

	case Q_SYNC:
		error = 0;
		break;

	default:
		error = EINVAL;
		break;
	}

	if (error)
		return (error);


	if ((uint)type >= MAXQUOTAS)
		return (EINVAL);
	if (vfs_busy(mp, LK_NOWAIT))
		return (0);

	switch (cmd) {

	case Q_QUOTAON:
		error = ufs_quotaon(cred, mp, type, arg);
		break;

	case Q_QUOTAOFF:
		error = ufs_quotaoff(mp, type);
		break;

	case Q_SETQUOTA:
		error = ufs_setquota(mp, uid, type, arg);
		break;

	case Q_SETUSE:
		error = ufs_setuse(mp, uid, type, arg);
		break;

	case Q_GETQUOTA:
		error = ufs_getquota(mp, uid, type, arg);
		break;

	case Q_SYNC:
		error = ufs_qsync(mp);
		break;

	default:
		error = EINVAL;
		break;
	}
	vfs_unbusy(mp);
	return (error);
#endif
}

/*
 * Initial UFS filesystems, done only once.
 */
int
ufs_init(struct vfsconf *vfsp)
{
	static int done;

	if (done)
		return (0);
	done = 1;
#ifdef QUOTA
	ufs_dqinit();
#endif
	return (0);
}

/*
 * This is the generic part of fhtovp called after the underlying
 * filesystem has validated the file handle.
 *
 * Call the VFS_CHECKEXP beforehand to verify access.
 */
int
ufs_fhtovp(struct mount *mp, struct vnode *rootvp,
	   struct ufid *ufhp, struct vnode **vpp)
{
	struct inode *ip;
	struct vnode *nvp;
	int error;

	error = VFS_VGET(mp, NULL, ufhp->ufid_ino, &nvp);
	if (error) {
		*vpp = NULLVP;
		return (error);
	}
	ip = VTOI(nvp);
	if (ip->i_mode == 0 ||
	    ip->i_gen != ufhp->ufid_gen ||
	    (VFSTOUFS(mp)->um_i_effnlink_valid ? ip->i_effnlink :
	    ip->i_nlink) <= 0) {
		vput(nvp);
		*vpp = NULLVP;
		return (ESTALE);
	}
	*vpp = nvp;
	return (0);
}


/*
 * This is the generic part of fhtovp called after the underlying
 * filesystem has validated the file handle.
 *
 * Verify that a host should have access to a filesystem.
 */
int
ufs_check_export(struct mount *mp, struct sockaddr *nam, int *exflagsp,
		 struct ucred **credanonp)
{
	struct netcred *np;
	struct ufsmount *ump;

	ump = VFSTOUFS(mp);
	/*
	 * Get the export permission structure for this <mp, client> tuple.
	 */
	np = vfs_export_lookup(mp, &ump->um_export, nam);
	if (np == NULL)
		return (EACCES);

	*exflagsp = np->netc_exflags;
	*credanonp = &np->netc_anon;
	return (0);
}
