/*
 * Copyright (c) 2018 The DragonFly Project.  All rights reserved.
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

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <kvm.h>
#include <stdio.h>

#include <sys/vnode.h>
#include <sys/mount.h>
#include <stddef.h>
#include <vfs/hammer2/hammer2.h>
#include <vfs/hammer2/hammer2_disk.h>

#include "fstat.h"

int
hammer2_filestat(struct vnode *vp, struct filestat *fsp)
{
	struct hammer2_inode ino;

	if (!kread(VTOI(vp), &ino, sizeof(ino))) {
		dprintf(stderr, "can't read hammer2_inode at %p for pid %d\n",
		    (void *)VTOI(vp), Pid);
		return 0;
	}

	fsp->fsid = fsp->rdev = fstat_dev2udev(vp->v_rdev);
	fsp->mode = ino.meta.mode | mtrans(vp->v_type);
	fsp->fileid = ino.meta.inum;
	fsp->size = ino.meta.size;
	return 1;
}
