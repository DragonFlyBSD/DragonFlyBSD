/*-
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Joerg Sonnenberger <joerg@bec.de>.
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
 */

#include "opt_compatdf12.h"

#include <sys/param.h>
#include <sys/kern_syscall.h>
#include <sys/mount.h>
#include <sys/nlookup.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/stat.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/vnode.h>
#include <emulation/dragonfly12/stat.h>

#include <sys/mplock2.h>

static void
cvtstat(struct dfbsd12_stat *oldstat, struct stat *newstat)
{
	bzero(oldstat, sizeof(*oldstat));

	oldstat->st_dev = newstat->st_dev;
	oldstat->st_ino = newstat->st_ino;	/* truncation */
	oldstat->st_mode = newstat->st_mode;
	oldstat->st_nlink = newstat->st_nlink;	/* truncation */
	oldstat->st_uid = newstat->st_uid;
	oldstat->st_gid = newstat->st_gid;
	oldstat->st_rdev = newstat->st_rdev;
	oldstat->st_atimespec = newstat->st_atimespec;
	oldstat->st_mtimespec = newstat->st_mtimespec;
	oldstat->st_ctimespec = newstat->st_ctimespec;
	oldstat->st_size = newstat->st_size;
	oldstat->st_blocks = newstat->st_blocks;
	oldstat->st_blksize = newstat->st_blksize;
	oldstat->st_flags = newstat->st_flags;
	oldstat->st_gen = newstat->st_gen;
}

/*
 * stat_args(char *path, struct dfbsd12_stat *ub)
 *
 * Get file status; this version follows links.
 *
 * MPALMOSTSAFE
 */
int
sys_dfbsd12_stat(struct dfbsd12_stat_args *uap)
{
	struct nlookupdata nd;
	struct dfbsd12_stat ost;
	struct stat st;
	int error;

	get_mplock();
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_stat(&nd, &st);
		if (error == 0) {
			cvtstat(&ost, &st);
			error = copyout(&ost, uap->ub, sizeof(ost));
		}
	}
	nlookup_done(&nd);
	rel_mplock();
	return (error);
}

/*
 * lstat_args(char *path, struct dfbsd12_stat *ub)
 *
 * Get file status; this version does not follow links.
 *
 * MPALMOSTSAFE
 */
int
sys_dfbsd12_lstat(struct dfbsd12_lstat_args *uap)
{
	struct nlookupdata nd;
	struct dfbsd12_stat ost;
	struct stat st;
	int error;

	get_mplock();
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		error = kern_stat(&nd, &st);
		if (error == 0) {
			cvtstat(&ost, &st);
			error = copyout(&ost, uap->ub, sizeof(ost));
		}
	}
	nlookup_done(&nd);
	rel_mplock();
	return (error);
}

/*
 * fhstat_args(struct fhandle *u_fhp, struct dfbsd12_stat *sb)
 *
 * MPALMOSTSAFE
 */
int
sys_dfbsd12_fhstat(struct dfbsd12_fhstat_args *uap)
{
	struct thread *td = curthread;
	struct dfbsd12_stat osb;
	struct stat sb;
	fhandle_t fh;
	struct mount *mp;
	struct vnode *vp;
	int error;

	/*
	 * Must be super user
	 */
	error = priv_check(td, PRIV_ROOT);
	if (error)
		return (error);
	
	error = copyin(uap->u_fhp, &fh, sizeof(fhandle_t));
	if (error)
		return (error);

	get_mplock();
	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL) {
		error = ESTALE;
		goto done;
	}
	if ((error = VFS_FHTOVP(mp, NULL, &fh.fh_fid, &vp)))
		goto done;
	error = vn_stat(vp, &sb, td->td_ucred);
	vput(vp);
	if (error)
		goto done;
	cvtstat(&osb, &sb);
	error = copyout(&osb, uap->sb, sizeof(osb));
done:
	rel_mplock();
	return (error);
}

/*
 * dfbsd12_fstat_args(int fd, struct dfbsd12_stat *sb)
 *
 * MPALMOSTSAFE
 */
int
sys_dfbsd12_fstat(struct dfbsd12_fstat_args *uap)
{
	struct dfbsd12_stat ost;
	struct stat st;
	int error;

	get_mplock();
	error = kern_fstat(uap->fd, &st);
	rel_mplock();

	if (error == 0) {
		cvtstat(&ost, &st);
		error = copyout(&ost, uap->sb, sizeof(ost));
	}
	return (error);
}
