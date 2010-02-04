/*
 * Copyright (c) 2000 Peter Wemm
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/vm/phys_pager.c,v 1.3.2.3 2000/12/17 02:05:41 alfred Exp $
 * $DragonFly: src/sys/vm/phys_pager.c,v 1.5 2006/03/27 01:54:18 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/linker_set.h>
#include <sys/conf.h>
#include <sys/mman.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_zone.h>

#include <sys/thread2.h>

static vm_object_t
phys_pager_alloc(void *handle, off_t size, vm_prot_t prot, off_t foff)
{
	vm_object_t object;

	/*
	 * Offset should be page aligned.
	 */
	if (foff & PAGE_MASK)
		return (NULL);

	size = round_page(size);

	KKASSERT(handle == NULL);
#if 0
	if (handle != NULL) {
		/*
		 * Lock to prevent object creation race condition.
		 */
		while (phys_pager_alloc_lock) {
			phys_pager_alloc_lock_want++;
			tsleep(&phys_pager_alloc_lock, 0, "ppall", 0);
			phys_pager_alloc_lock_want--;
		}
		phys_pager_alloc_lock = 1;
	
		/*
		 * Look up pager, creating as necessary.
		 */
		object = vm_pager_object_lookup(&phys_pager_object_list, handle);
		if (object == NULL) {
			/*
			 * Allocate object and associate it with the pager.
			 */
			object = vm_object_allocate(OBJT_PHYS,
				OFF_TO_IDX(foff + size));
			object->handle = handle;
			TAILQ_INSERT_TAIL(&phys_pager_object_list, object,
			    pager_object_list);
		} else {
			/*
			 * Gain a reference to the object.
			 */
			vm_object_reference(object);
			if (OFF_TO_IDX(foff + size) > object->size)
				object->size = OFF_TO_IDX(foff + size);
		}
		phys_pager_alloc_lock = 0;
		if (phys_pager_alloc_lock_want)
			wakeup(&phys_pager_alloc_lock);
	} else { ... }
#endif
	object = vm_object_allocate(OBJT_PHYS, OFF_TO_IDX(foff + size));

	return (object);
}

static void
phys_pager_dealloc(vm_object_t object)
{
	KKASSERT(object->handle == NULL);
	KKASSERT(object->swblock_count == 0);
}

static int
phys_pager_getpage(vm_object_t object, vm_page_t *mpp, int seqaccess)
{
	vm_page_t m = *mpp;

	crit_enter();
	if ((m->flags & PG_ZERO) == 0)
		vm_page_zero_fill(m);
	vm_page_flag_set(m, PG_ZERO);
	/* Switch off pv_entries */
	vm_page_unmanage(m);
	m->valid = VM_PAGE_BITS_ALL;
	m->dirty = 0;
	crit_exit();

	return (VM_PAGER_OK);
}

static void
phys_pager_putpages(vm_object_t object, vm_page_t *m, int count,
		    boolean_t sync, int *rtvals)
{

	panic("phys_pager_putpage called");
}

/*
 * Implement a pretty aggressive clustered getpages strategy.  Hint that
 * everything in an entire 4MB window should be prefaulted at once.
 *
 * XXX 4MB (1024 slots per page table page) is convenient for x86,
 * but may not be for other arches.
 */
#ifndef PHYSCLUSTER
#define PHYSCLUSTER 1024
#endif

static boolean_t
phys_pager_haspage(vm_object_t object, vm_pindex_t pindex)
{
	return (TRUE);
}

struct pagerops physpagerops = {
	phys_pager_alloc,
	phys_pager_dealloc,
	phys_pager_getpage,
	phys_pager_putpages,
	phys_pager_haspage
};
