/*
 * Copyright (c) 2003, 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Hiten Pandya <hmp@backplane.com>.
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
 */
/*
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *	from: @(#)vm_page.c	7.4 (Berkeley) 5/7/91
 * $DragonFly: src/sys/vm/vm_contig.c,v 1.8 2004/07/16 05:04:36 hmp Exp $
 */

/*
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * Contiguous memory allocation API.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_page2.h>

/*
 * vm_contig_pg_clean:
 * 
 * Do a thorough cleanup of the specified 'queue', which can be either
 * PQ_ACTIVE or PQ_INACTIVE by doing a walkthrough.  If the page is not
 * marked dirty, it is shoved into the page cache, provided no one has
 * currently aqcuired it, otherwise localized action per object type
 * is taken for cleanup:
 *
 * 	In the OBJT_VNODE case, the whole page range is cleaned up
 * 	using the vm_object_page_clean() routine, by specyfing a
 * 	start and end of '0'.
 *
 * 	Otherwise if the object is of any other type, the generic
 * 	pageout (daemon) flush routine is invoked.
 */
static int
vm_contig_pg_clean(int queue)
{
	vm_object_t object;
	vm_page_t m, m_tmp, next;

	for (m = TAILQ_FIRST(&vm_page_queues[queue].pl); m != NULL; m = next) {
		KASSERT(m->queue == queue,
			("vm_contig_clean: page %p's queue is not %d", m, queue));
		
		next = TAILQ_NEXT(m, pageq);
		
		if (vm_page_sleep_busy(m, TRUE, "vpctw0"))
			return (TRUE);
		
		vm_page_test_dirty(m);
		if (m->dirty) {
			object = m->object;
			if (object->type == OBJT_VNODE) {
				vn_lock(object->handle, NULL,
					LK_EXCLUSIVE | LK_RETRY, curthread);
				vm_object_page_clean(object, 0, 0, OBJPC_SYNC);
				VOP_UNLOCK(object->handle, NULL, 0, curthread);
				return (TRUE);
			} else if (object->type == OBJT_SWAP ||
					object->type == OBJT_DEFAULT) {
				m_tmp = m;
				vm_pageout_flush(&m_tmp, 1, 0);
				return (TRUE);
			}
		}
		
		if ((m->dirty == 0) && (m->busy == 0) && (m->hold_count == 0))
			vm_page_cache(m);
	}

	return (FALSE);
}

/*
 * vm_contig_pg_alloc:
 *
 * Allocate contiguous pages from the VM.  This function does not
 * map the allocated pages into the kernel map, otherwise it is
 * impossible to make large allocations (i.e. >2G).
 *
 * Malloc()'s data structures have been used for collection of
 * statistics and for allocations of less than a page.
 *
 */
int
vm_contig_pg_alloc(
	unsigned long size,
	vm_paddr_t low,
	vm_paddr_t high,
	unsigned long alignment,
	unsigned long boundary)
{
	int i, s, start, pass;
	vm_offset_t phys;
	vm_page_t pga = vm_page_array;

	size = round_page(size);
	if (size == 0)
		panic("vm_contig_pg_alloc: size must not be 0");
	if ((alignment & (alignment - 1)) != 0)
		panic("vm_contig_pg_alloc: alignment must be a power of 2");
	if ((boundary & (boundary - 1)) != 0)
		panic("vm_contig_pg_alloc: boundary must be a power of 2");

	start = 0;
	for (pass = 0; pass <= 1; pass++) {
		s = splvm();
again:
		/*
		 * Find first page in array that is free, within range, aligned, and
		 * such that the boundary won't be crossed.
		 */
		for (i = start; i < vmstats.v_page_count; i++) {
			int pqtype;
			phys = VM_PAGE_TO_PHYS(&pga[i]);
			pqtype = pga[i].queue - pga[i].pc;
			if (((pqtype == PQ_FREE) || (pqtype == PQ_CACHE)) &&
			    (phys >= low) && (phys < high) &&
			    ((phys & (alignment - 1)) == 0) &&
			    (((phys ^ (phys + size - 1)) & ~(boundary - 1)) == 0))
				break;
		}

		/*
		 * If we cannot find the page in the given range, or we have
		 * crossed the boundary, call the vm_contig_pg_clean() function
		 * for flushing out the queues, and returning it back to
		 * normal state.
		 */
		if ((i == vmstats.v_page_count) ||
			((VM_PAGE_TO_PHYS(&pga[i]) + size) > high)) {

again1:
			if (vm_contig_pg_clean(PQ_INACTIVE))
				goto again1;
			if (vm_contig_pg_clean(PQ_ACTIVE))
				goto again1;

			splx(s);
			continue;	/* next pass */
		}
		start = i;

		/*
		 * Check successive pages for contiguous and free.
		 */
		for (i = start + 1; i < (start + size / PAGE_SIZE); i++) {
			int pqtype;
			pqtype = pga[i].queue - pga[i].pc;
			if ((VM_PAGE_TO_PHYS(&pga[i]) !=
			    (VM_PAGE_TO_PHYS(&pga[i - 1]) + PAGE_SIZE)) ||
			    ((pqtype != PQ_FREE) && (pqtype != PQ_CACHE))) {
				start++;
				goto again;
			}
		}

		for (i = start; i < (start + size / PAGE_SIZE); i++) {
			int pqtype;
			vm_page_t m = &pga[i];

			pqtype = m->queue - m->pc;
			if (pqtype == PQ_CACHE) {
				vm_page_busy(m);
				vm_page_free(m);
			}
			vm_page_unqueue_nowakeup(m);
			m->valid = VM_PAGE_BITS_ALL;
			if (m->flags & PG_ZERO)
				vm_page_zero_count--;
			/* Don't clear the PG_ZERO flag, we'll need it later. */
			m->flags &= PG_ZERO;
			KASSERT(m->dirty == 0,
				("vm_contig_pg_alloc: page %p was dirty", m));
			m->wire_count = 0;
			m->busy = 0;
			m->object = NULL;
		}

		/*
		 * Our job is done, return the index page of vm_page_array.
		 */

		splx(s);
		return (start); /* aka &pga[start] */
	}

	/*
	 * Failed.
	 */
	splx(s);
	return (-1);
}

/*
 * vm_contig_pg_free:
 *
 * Remove pages previously allocated by vm_contig_pg_alloc, and
 * assume all references to the pages have been removed, and that
 * it is OK to add them back to the free list.
 */
void
vm_contig_pg_free(int start, u_long size)
{
	vm_page_t pga = vm_page_array;
	int i;
	
	size = round_page(size);
	if (size == 0)
		panic("vm_contig_pg_free: size must not be 0");

	for (i = start; i < (start + size / PAGE_SIZE); i++) {
		vm_page_free(&pga[i]);
	}
}

/*
 * vm_contig_pg_kmap:
 *
 * Map previously allocated (vm_contig_pg_alloc) range of pages from
 * vm_page_array[] into the KVA.  Once mapped, the pages are part of
 * the Kernel, and are to free'ed with kmem_free(kernel_map, addr, size).
 */
vm_offset_t
vm_contig_pg_kmap(int start, u_long size, vm_map_t map, int flags)
{
	vm_offset_t addr, tmp_addr;
	vm_page_t pga = vm_page_array;
	int i, s, count;

	size = round_page(size);
	if (size == 0)
		panic("vm_contig_pg_kmap: size must not be 0");

	s = splvm();	/* XXX: is this really needed? */

	/*
	 * We've found a contiguous chunk that meets our requirements.
	 * Allocate KVM, and assign phys pages and return a kernel VM
	 * pointer.
	 */
	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);
	if (vm_map_findspace(map, vm_map_min(map), size, 1, &addr) !=
	    KERN_SUCCESS) {
		/*
		 * XXX We almost never run out of kernel virtual
		 * space, so we don't make the allocated memory
		 * above available.
		 */
		vm_map_unlock(map);
		vm_map_entry_release(count);
 		splx(s);
		return (0);
	}
	vm_object_reference(kernel_object);
	vm_map_insert(map, &count, 
	    kernel_object, addr - VM_MIN_KERNEL_ADDRESS,
	    addr, addr + size, VM_PROT_ALL, VM_PROT_ALL, 0);
	vm_map_unlock(map);
	vm_map_entry_release(count);

	tmp_addr = addr;
	for (i = start; i < (start + size / PAGE_SIZE); i++) {
		vm_page_t m = &pga[i];
		vm_page_insert(m, kernel_object,
			OFF_TO_IDX(tmp_addr - VM_MIN_KERNEL_ADDRESS));
		if ((flags & M_ZERO) && !(m->flags & PG_ZERO))
			pmap_zero_page(VM_PAGE_TO_PHYS(m));
		m->flags = 0;
		tmp_addr += PAGE_SIZE;
 	}
	vm_map_wire(map, addr, addr + size, 0);

	splx(s);
	return (addr);
}

void *
contigmalloc(
	unsigned long size,	/* should be size_t here and for malloc() */
	struct malloc_type *type,
	int flags,
	vm_paddr_t low,
	vm_paddr_t high,
	unsigned long alignment,
	unsigned long boundary)
{
	return contigmalloc_map(size, type, flags, low, high, alignment,
			boundary, kernel_map);
}

void *
contigmalloc_map(
	unsigned long size,	/* should be size_t here and for malloc() */
	struct malloc_type *type,
	int flags,
	vm_paddr_t low,
	vm_paddr_t high,
	unsigned long alignment,
	unsigned long boundary,
	vm_map_t map)
{
	int index;
	void *rv;

	index = vm_contig_pg_alloc(size, low, high, alignment, boundary);
	if (index < 0) {
		printf("contigmalloc_map: failed in index < 0 case!");
		return NULL;
	}

	rv = (void *) vm_contig_pg_kmap(index, size, map, flags);
	if (!rv)
		vm_contig_pg_free(index, size);
	
	return rv;
}

void
contigfree(void *addr, unsigned long size, struct malloc_type *type)
{
	kmem_free(kernel_map, (vm_offset_t)addr, size);
}

vm_offset_t
vm_page_alloc_contig(
	vm_offset_t size,
	vm_paddr_t low,
	vm_paddr_t high,
	vm_offset_t alignment)
{
	return ((vm_offset_t)contigmalloc_map(size, M_DEVBUF, M_NOWAIT, low,
				high, alignment, 0ul, kernel_map));
}
