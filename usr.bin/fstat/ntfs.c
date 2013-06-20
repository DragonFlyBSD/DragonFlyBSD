/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
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

#define _KERNEL_STRUCTURES
#include <sys/vnode.h>
#include <sys/mount.h>
#undef _KERNEL_STRUCTURES

#include <vfs/ntfs/ntfs.h>
#include <vfs/ntfs/ntfs_inode.h>

#include <stdio.h>
#include <kvm.h>

#include "fstat.h"

int
ntfs_filestat(struct vnode *vp, struct filestat *fsp)
{
	struct fnode fn;
	struct ntnode ino;
	struct ntfsmount mp;

	if (!kread(VTOF(vp), &fn, sizeof(fn))) {
		dprintf(stderr, "can't read fnode at %p for pid %d\n",
		    (void *)VTOF(vp), Pid);
		return 0;
	}

	if (!kread(FTONT(&fn), &ino, sizeof(ino))) {
		dprintf(stderr, "can't read ntnode at %p for pid %d\n",
		    (void *)FTONT(&fn), Pid);
		return 0;
	}

	if (!kread(ino.i_mp, &mp, sizeof(mp))) {
		dprintf(stderr, "can't read ntfsmount at %p for pid %d\n",
		    (void *)ino.i_mp, Pid);
		return 0;
	}
	fsp->mode = mp.ntm_mode | mtrans(vp->v_type);
	fsp->rdev = fsp->fsid = dev2udev(ino.i_dev);
	fsp->size = fn.f_size;
	fsp->fileid = ino.i_number;

	return 1;
}
