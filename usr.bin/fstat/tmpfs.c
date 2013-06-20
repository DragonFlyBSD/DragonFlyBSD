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

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <kvm.h>
#include <stdio.h>

#define _KERNEL_STRUCTURES
#include <sys/vnode.h>
#include <vfs/tmpfs/tmpfs.h>
#undef _KERNEL_STRUCTURES

#include "fstat.h"

int
tmpfs_filestat(struct vnode *vp, struct filestat *fsp)
{
	struct tmpfs_node ino;
	struct mount mount_l;

	if (!kread(VTOI(vp), &ino, sizeof(ino))) {
		dprintf(stderr, "can't read struct tmpfs_node at %p for pid %d\n",
		    VTOI(vp), Pid);
		return 0;
	}
	if (!kread(vp->v_mount, &mount_l, sizeof(struct mount))) {
		dprintf(stderr, "can't read struct mount at %p for pid %d\n",
		    vp->v_mount, Pid);
		return 0;
	}
	fsp->fsid = mount_l.mnt_stat.f_fsid.val[0];
	fsp->mode = ino.tn_mode | mtrans(ino.tn_type);
	fsp->fileid = ino.tn_id;
	fsp->size = ino.tn_size;
	fsp->rdev = ino.tn_spec.tn_rdev;
	return 1;
}
