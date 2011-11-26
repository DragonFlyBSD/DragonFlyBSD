/*
 * 43BSD_STATS.C	- 4.3BSD compatibility stats syscalls
 *
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
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
 * $DragonFly: src/sys/emulation/43bsd/43bsd_stats.c,v 1.5 2006/06/05 07:26:07 dillon Exp $
 * 	from: DragonFly kern/kern_descrip.c,v 1.16
 *	from: DragonFly kern/vfs_syscalls.c,v 1.21
 *
 * These syscalls used to live in kern/kern_descrip.c and
 * kern/vfs_syscalls.c.  They are modified * to use the new split syscalls.
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/kern_syscall.h>
#include <sys/namei.h>
#include <sys/nlookup.h>

#include <sys/mplock2.h>

#include <emulation/43bsd/stat.h>

static int
compat_43_copyout_stat(struct stat *st, struct ostat *uaddr)
{
	struct ostat ost;
	int error;

	ost.st_dev = st->st_dev;
	ost.st_ino = st->st_ino;
	ost.st_mode = st->st_mode;
	ost.st_nlink = st->st_nlink;
	ost.st_uid = st->st_uid;
	ost.st_gid = st->st_gid;
	ost.st_rdev = st->st_rdev;
	if (st->st_size < (quad_t)1 << 32)
		ost.st_size = st->st_size;
	else
		ost.st_size = -2;
	ost.st_atime = st->st_atime;
	ost.st_mtime = st->st_mtime;
	ost.st_ctime = st->st_ctime;
	ost.st_blksize = st->st_blksize;
	ost.st_blocks = st->st_blocks;
	ost.st_flags = st->st_flags;
	ost.st_gen = st->st_gen;

	error = copyout(&ost, uaddr, sizeof(ost));
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_ofstat(struct ofstat_args *uap)
{
	struct stat st;
	int error;

	error = kern_fstat(uap->fd, &st);

	if (error == 0)
		error = compat_43_copyout_stat(&st, uap->sb);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_ostat(struct ostat_args *uap)
{
	struct nlookupdata nd;
	struct stat st;
	int error;

	get_mplock();
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_stat(&nd, &st);
		if (error == 0)
			error = compat_43_copyout_stat(&st, uap->ub);
		nlookup_done(&nd);
	}
	rel_mplock();
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_olstat(struct olstat_args *uap)
{
	struct nlookupdata nd;
	struct stat st;
	int error;

	get_mplock();
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, 0);
	if (error == 0) {
		error = kern_stat(&nd, &st);
		if (error == 0)
			error = compat_43_copyout_stat(&st, uap->ub);
		nlookup_done(&nd);
	}
	rel_mplock();
	return (error);
}
