/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)ffs_vnops.c	8.15 (Berkeley) 5/14/95
 * $FreeBSD: src/sys/ufs/ffs/ffs_vnops.c,v 1.64 2000/01/10 12:04:25 phk Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/conf.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>

#include <sys/buf2.h>

#include "quota.h"
#include "inode.h"
#include "ufsmount.h"
#include "ufs_extern.h"

#include "fs.h"
#include "ffs_extern.h"

static int	ffs_fsync (struct vop_fsync_args *);
static int	ffs_read (struct vop_read_args *);
static int	ffs_write (struct vop_write_args *);

/* Global vfs data structures for ufs. */
struct vop_ops ffs_vnode_vops = {
	.vop_default =		ufs_vnoperate,
	.vop_fsync =		ffs_fsync,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
	.vop_read =		ffs_read,
	.vop_balloc =		ffs_balloc,
	.vop_reallocblks =	ffs_reallocblks,
	.vop_write =		ffs_write
};

struct vop_ops ffs_spec_vops = {
	.vop_default =		ufs_vnoperatespec,
	.vop_fsync =		ffs_fsync
};

struct vop_ops ffs_fifo_vops = {
	.vop_default =		ufs_vnoperatefifo,
	.vop_fsync =		ffs_fsync
};

#include "ufs_readwrite.c"

/*
 * Synch an open file.
 *
 * ffs_fsync(struct vnode *a_vp, struct ucred *a_cred, int a_waitfor,
 *	     struct proc *a_p)
 */

static int ffs_checkdeferred(struct buf *bp);

/* ARGSUSED */
static int
ffs_fsync(struct vop_fsync_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int error;

	if (vn_isdisk(vp, NULL))
		if (vp->v_rdev && vp->v_rdev->si_mountpoint != NULL &&
		    (vp->v_rdev->si_mountpoint->mnt_flag & MNT_SOFTDEP))
			softdep_fsync_mountdev(vp);

	/*
	 * Flush all dirty buffers associated with a vnode.
	 */
	error = vfsync(vp, ap->a_waitfor, NIADDR + 1, ffs_checkdeferred,
		       softdep_sync_metadata);
	if (error == 0)
		error = ffs_update(vp, (ap->a_waitfor == MNT_WAIT));
	return (error);
}

static int
ffs_checkdeferred(struct buf *bp)
{
	if (LIST_FIRST(&bp->b_dep) != NULL &&
	    (bp->b_flags & B_DEFERRED) == 0 &&
	    buf_countdeps(bp, 0)) {
		bp->b_flags |= B_DEFERRED;
		return(1);
	}
	return(0);
}
