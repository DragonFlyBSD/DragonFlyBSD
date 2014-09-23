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
 *	@(#)vfs_lookup.c	8.4 (Berkeley) 2/16/94
 * $FreeBSD: src/sys/kern/vfs_lookup.c,v 1.38.2.3 2001/08/31 19:36:49 dillon Exp $
 */

#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/sysctl.h>

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include <vm/vm_zone.h>

int varsym_enable = 0;
SYSCTL_INT(_vfs, OID_AUTO, varsym_enable, CTLFLAG_RW, &varsym_enable, 0,
	    "Enable Variant Symlinks");

/*
 * OLD API FUNCTION
 *
 * Relookup a path name component.  This function is only used by the
 * old API *_rename() code under very specific conditions.  CNP_LOCKPARENT
 * must be set in the cnp.  dvp must be unlocked on entry and will be left
 * unlocked if an error occurs.
 *
 * The returned *vpp will be NULL if an error occurs.  Note that no error
 * will occur (error == 0) for CREATE requests on a non-existant target, but
 * *vpp will be NULL in that case.  Both dvp and *vpp (if not NULL) will be
 * locked in the no-error case.   No additional references are made to dvp,
 * only the locking state changes.
 */
int
relookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp)
{
	int rdonly;			/* lookup read-only flag bit */
	int error = 0;

	/*
	 * Setup: break out flag bits into variables.
	 */
	KKASSERT(cnp->cn_flags & CNP_LOCKPARENT);
	KKASSERT(cnp->cn_flags & CNP_PDIRUNLOCK);
	vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
	cnp->cn_flags &= ~CNP_PDIRUNLOCK;

	*vpp = NULL;
	rdonly = cnp->cn_flags & CNP_RDONLY;

	/*
	 * Search a new directory.
	 */

	/*
	 * Degenerate lookups (e.g. "/", "..", derived from "/.", etc)
	 * are not allowed.
	 */
	if (cnp->cn_nameptr[0] == '\0') {
		error = EISDIR;
		goto bad;
	}
	if (cnp->cn_flags & CNP_ISDOTDOT)
		panic ("relookup: lookup on dot-dot");

	/*
	 * We now have a segment name to search for, and a directory to search.
	 */
	if ((error = VOP_OLD_LOOKUP(dvp, vpp, cnp)) != 0) {
		KASSERT(*vpp == NULL, ("leaf should be empty"));
		if (error != EJUSTRETURN)
			goto bad;
		/*
		 * If creating and at end of pathname, then can consider
		 * allowing file to be created.
		 */
		if (rdonly) {
			error = EROFS;
			goto bad;
		}
		/*
		 * We return with ni_vp NULL to indicate that the entry
		 * doesn't currently exist, leaving a pointer to the
		 * (possibly locked) directory inode in ndp->ni_dvp.
		 */
		return (0);
	}

	/*
	 * Check for symbolic link
	 */
	KASSERT((*vpp)->v_type != VLNK || !(cnp->cn_flags & CNP_FOLLOW),
	    ("relookup: symlink found."));

	/*
	 * Disallow directory write attempts on read-only file systems.
	 */
	if (rdonly && (cnp->cn_nameiop == NAMEI_DELETE || 
			cnp->cn_nameiop == NAMEI_RENAME)) {
		error = EROFS;
		goto bad;
	}
	KKASSERT((cnp->cn_flags & CNP_PDIRUNLOCK) == 0);
	return (0);

bad:
	if ((cnp->cn_flags & CNP_PDIRUNLOCK) == 0) {
		cnp->cn_flags |= CNP_PDIRUNLOCK;
		vn_unlock(dvp);
	}
	if (*vpp) {
		vput(*vpp);
		*vpp = NULL;
	}
	return (error);
}
