/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * VM range locking module.  Range locks within VM objects are used to protect
 * I/O operations and will eventually also be used in an extended form
 * for cache coherency control.
 *
 * $DragonFly: src/sys/vm/vm_rangelock.c,v 1.1 2003/07/22 17:03:35 dillon Exp $
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>		/* for curproc, pageproc */
#include <sys/vnode.h>
#include <sys/vmmeter.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/swap_pager.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

/*
 * Note: these routines are currently greatly simplified and unoptimized.
 *
 * Locks are sorted by base address.
 */
void
shlock_range(vm_object_lock_t lock, vm_object_t object, off_t base, off_t bytes)
{
    struct vm_object_lock **plock;
    vm_object_lock_t scan;

    /*
     * Locate the insertion point and check for conflicts
     */
again:
    for (plock = &object->range_locks;
	(scan = *plock) != NULL;
	plock = &scan->next
    ) {
	if (base < scan->base + scan->bytes && base + bytes > scan->base) {
	    if (scan->type != VMOBJ_LOCK_SHARED) {
		tsleep(scan
	    }
	}
    }
}

void
exlock_range(vm_object_lock_t lock, vm_object_t object, off_t base, off_t bytes)
{
}

void
unlock_range(vm_object_lock_t lock)
{
}

