/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 *
 * $DragonFly: src/sys/kern/vfs_rangelock.c,v 1.1 2004/12/17 00:18:07 dillon Exp $
 */
/*
 * This module implements hard range locks for files and directories.  It is
 * not to be confused with the UNIX advisory lock mechanism.  This module
 * will allow the kernel and VFS to break large I/O requests into smaller
 * pieces without losing atomicy guarentees and, eventually, this module will
 * be responsible for providing hooks for remote cache coherency protocols
 * as well.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/poll.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>

static void vrange_lock_overlapped(struct vnode *vp, 
			struct vrangelock *vr, struct vrangelock *scan);
static int vrange_lock_conflicted(struct vnode *vp, struct vrangelock *vr);

/*
 * Lock a range within a vnode.
 *
 * The lock list is sorted by vr_offset.
 */
void
vrange_lock(struct vnode *vp, struct vrangelock *vr)
{
    struct vrangelock *scan;
    off_t eoff;

    eoff = vr->vr_offset + vr->vr_length;

    KKASSERT((vr->vr_flags & RNGL_ONLIST) == 0);
    vr->vr_flags |= RNGL_ONLIST;

    TAILQ_FOREACH(scan, &vp->v_range.vh_list, vr_node) {
	/*
	 * If the new element is entirely in front of the scan element
	 * we are done.  If it is entirely beyond the scan element we
	 * loop.  Otherwise an overlap has occured.
	 */
	if (eoff <= scan->vr_offset) {
	    TAILQ_INSERT_BEFORE(scan, vr, vr_node);
	    return;
	}
	if (vr->vr_offset >= scan->vr_offset + scan->vr_length)
	    continue;
	vrange_lock_overlapped(vp, vr, scan);
    }
    TAILQ_INSERT_TAIL(&vp->v_range.vh_list, vr, vr_node);
}

/*
 * An overlap occured.  The request is still inserted sorted based on
 * vr_offset but we must also scan the conflict space and block while
 * conflicts exist.
 */
static void
vrange_lock_overlapped(struct vnode *vp, 
			struct vrangelock *vr, struct vrangelock *scan)
{
    int conflicted = 0;
    int inserted = 0;
    int warned = 0;
    off_t eoff;

    eoff = vr->vr_offset + vr->vr_length;

    while (scan->vr_offset < eoff) {
	if ((vr->vr_flags & scan->vr_flags & RNGL_SHARED) == 0) {
	    scan->vr_flags |= RNGL_CHECK;
	    vr->vr_flags |= RNGL_WAITING;
	    conflicted = 1;
	}
	if (inserted == 0 && vr->vr_offset < scan->vr_offset) {
	    TAILQ_INSERT_BEFORE(scan, vr, vr_node);
	    inserted = 1;
	}
	if ((scan = TAILQ_NEXT(scan, vr_node)) == NULL) {
	    if (inserted == 0)
		TAILQ_INSERT_TAIL(&vp->v_range.vh_list, vr, vr_node);
	    break;
	}
    }

    /*
     * sleep until the conflict has been resolved.
     */
    while (conflicted) {
	if (tsleep(&vp->v_range.vh_list, 0, "vrnglk", hz * 3) == EWOULDBLOCK) {
	    if (warned == 0)
		printf("warning: conflicted lock vp %p %lld,%lld blocked\n",
		    vp, vr->vr_offset, vr->vr_length);
	    warned = 1;
	}
	conflicted = vrange_lock_conflicted(vp, vr);
    }
    if (warned) {
	printf("waring: conflicted lock vp %p %lld,%lld unblocked\n",
	    vp, vr->vr_offset, vr->vr_length);
    }
}

/*
 * Check for conflicts by scanning both forwards and backwards from the
 * node in question.  The list is sorted by vr_offset but ending offsets
 * may vary.  Because of this, the reverse scan cannot stop early.
 *
 * Return 0 on success, 1 if the lock is still conflicted.  We do not
 * check elements that are waiting as that might result in a deadlock.
 * We can stop the moment we hit a conflict.
 */
static int
vrange_lock_conflicted(struct vnode *vp, struct vrangelock *vr)
{
    struct vrangelock *scan;
    off_t eoff;

    eoff = vr->vr_offset + vr->vr_length;

    KKASSERT(vr->vr_flags & RNGL_WAITING);
    scan = vr;
    while ((scan = TAILQ_PREV(scan, vrangelock_list, vr_node)) != NULL) {
	if (scan->vr_flags & RNGL_WAITING)
		continue;
	if (scan->vr_offset + scan->vr_length > vr->vr_offset) {
	    if ((vr->vr_flags & scan->vr_flags & RNGL_SHARED) == 0) {
		scan->vr_flags |= RNGL_CHECK;
		return(1);
	    }
	}
    }
    scan = vr;
    while ((scan = TAILQ_NEXT(scan, vr_node)) != NULL) {
	if (eoff <= scan->vr_offset)
	    break;
	if (scan->vr_flags & RNGL_WAITING)
	    continue;
	if ((vr->vr_flags & scan->vr_flags & RNGL_SHARED) == 0) {
	    scan->vr_flags |= RNGL_CHECK;
	    return(1);
	}
    }
    vr->vr_flags &= ~RNGL_WAITING;
    return(0);
}

void
vrange_unlock(struct vnode *vp, struct vrangelock *vr)
{
    KKASSERT((vr->vr_flags & RNGL_ONLIST) != 0);
    vr->vr_flags &= ~RNGL_ONLIST;
    TAILQ_REMOVE(&vp->v_range.vh_list, vr, vr_node);
    if (vr->vr_flags & RNGL_CHECK) {
	vr->vr_flags &= ~RNGL_CHECK;
	wakeup(&vp->v_range.vh_list);
    }
}

