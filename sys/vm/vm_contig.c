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
 * $DragonFly: src/sys/vm/vm_contig.c,v 1.21 2006/12/28 21:24:02 dillon Exp $
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

#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <vm/vm_page2.h>

static void vm_contig_pg_free(int start, u_long size);

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
static void
vm_contig_pg_clean(int queue, int count)
{
	vm_object_t object;
	vm_page_t m, m_tmp;
	struct vm_page marker;
	struct vpgqueues *pq = &vm_page_queues[queue];

	/*
	 * Setup a local marker
	 */
	bzero(&marker, sizeof(marker));
	marker.flags = PG_BUSY | PG_FICTITIOUS | PG_MARKER;
	marker.queue = queue;
	marker.wire_count = 1;

	vm_page_queues_spin_lock(queue);
	TAILQ_INSERT_HEAD(&pq->pl, &marker, pageq);
	vm_page_queues_spin_unlock(queue);

	/*
	 * Iterate the queue.  Note that the vm_page spinlock must be
	 * acquired before the pageq spinlock so it's easiest to simply
	 * not hold it in the loop iteration.
	 */
	while (count-- > 0 && (m = TAILQ_NEXT(&marker, pageq)) != NULL) {
		vm_page_and_queue_spin_lock(m);
		if (m != TAILQ_NEXT(&marker, pageq)) {
			vm_page_and_queue_spin_unlock(m);
			++count;
			continue;
		}
		KKASSERT(m->queue == queue);

		TAILQ_REMOVE(&pq->pl, &marker, pageq);
		TAILQ_INSERT_AFTER(&pq->pl, m, &marker, pageq);

		if (m->flags & PG_MARKER) {
			vm_page_and_queue_spin_unlock(m);
			continue;
		}
		if (vm_page_busy_try(m, TRUE)) {
			vm_page_and_queue_spin_unlock(m);
			continue;
		}
		vm_page_and_queue_spin_unlock(m);

		/*
		 * We've successfully busied the page
		 */
		if (m->queue - m->pc != queue) {
			vm_page_wakeup(m);
			continue;
		}
		if (m->wire_count || m->hold_count) {
			vm_page_wakeup(m);
			continue;
		}
		if ((object = m->object) == NULL) {
			vm_page_wakeup(m);
			continue;
		}
		vm_page_test_dirty(m);
		if (m->dirty || (m->flags & PG_NEED_COMMIT)) {
			vm_object_hold(object);
			KKASSERT(m->object == object);

			if (object->type == OBJT_VNODE) {
				vm_page_wakeup(m);
				vn_lock(object->handle, LK_EXCLUSIVE|LK_RETRY);
				vm_object_page_clean(object, 0, 0, OBJPC_SYNC);
				vn_unlock(((struct vnode *)object->handle));
			} else if (object->type == OBJT_SWAP ||
					object->type == OBJT_DEFAULT) {
				m_tmp = m;
				vm_pageout_flush(&m_tmp, 1, 0);
			} else {
				vm_page_wakeup(m);
			}
			vm_object_drop(object);
		} else if (m->hold_count == 0) {
			vm_page_cache(m);
		} else {
			vm_page_wakeup(m);
		}
	}

	/*
	 * Scrap our local marker
	 */
	vm_page_queues_spin_lock(queue);
	TAILQ_REMOVE(&pq->pl, &marker, pageq);
	vm_page_queues_spin_unlock(queue);
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
 */
static int
vm_contig_pg_alloc(unsigned long size, vm_paddr_t low, vm_paddr_t high,
		   unsigned long alignment, unsigned long boundary, int mflags)
{
	int i, q, start, pass;
	vm_offset_t phys;
	vm_page_t pga = vm_page_array;
	vm_page_t m;
	int pqtype;

	size = round_page(size);
	if (size == 0)
		panic("vm_contig_pg_alloc: size must not be 0");
	if ((alignment & (alignment - 1)) != 0)
		panic("vm_contig_pg_alloc: alignment must be a power of 2");
	if ((boundary & (boundary - 1)) != 0)
		panic("vm_contig_pg_alloc: boundary must be a power of 2");

	/*
	 * See if we can get the pages from the contiguous page reserve
	 * alist.  The returned pages will be allocated and wired but not
	 * busied.
	 */
	m = vm_page_alloc_contig(
		low, high, alignment, boundary, size, VM_MEMATTR_DEFAULT);
	if (m)
		return (m - &pga[0]);

	/*
	 * Three passes (0, 1, 2).  Each pass scans the VM page list for
	 * free or cached pages.  After each pass if the entire scan failed
	 * we attempt to flush inactive pages and reset the start index back
	 * to 0.  For passes 1 and 2 we also attempt to flush active pages.
	 */
	start = 0;
	for (pass = 0; pass < 3; pass++) {
		/*
		 * Find first page in array that is free, within range, 
		 * aligned, and such that the boundary won't be crossed.
		 */
again:
		for (i = start; i < vmstats.v_page_count; i++) {
			m = &pga[i];
			phys = VM_PAGE_TO_PHYS(m);
			pqtype = m->queue - m->pc;
			if (((pqtype == PQ_FREE) || (pqtype == PQ_CACHE)) &&
			    (phys >= low) && (phys < high) &&
			    ((phys & (alignment - 1)) == 0) &&
			    (((phys ^ (phys + size - 1)) & ~(boundary - 1)) == 0) &&
			    m->busy == 0 && m->wire_count == 0 &&
			    m->hold_count == 0 &&
			    (m->flags & (PG_BUSY | PG_NEED_COMMIT)) == 0)
			{
				break;
			}
		}

		/*
		 * If we cannot find the page in the given range, or we have
		 * crossed the boundary, call the vm_contig_pg_clean() function
		 * for flushing out the queues, and returning it back to
		 * normal state.
		 */
		if ((i == vmstats.v_page_count) ||
		    ((VM_PAGE_TO_PHYS(&pga[i]) + size) > high)) {

			/*
			 * Best effort flush of all inactive pages.
			 * This is quite quick, for now stall all
			 * callers, even if they've specified M_NOWAIT.
			 */
			for (q = 0; q < PQ_L2_SIZE; ++q) {
				vm_contig_pg_clean(PQ_INACTIVE + q,
						   vmstats.v_inactive_count);
				lwkt_yield();
			}

			/*
			 * Best effort flush of active pages.
			 *
			 * This is very, very slow.
			 * Only do this if the caller has agreed to M_WAITOK.
			 *
			 * If enough pages are flushed, we may succeed on
			 * next (final) pass, if not the caller, contigmalloc(),
			 * will fail in the index < 0 case.
			 */
			if (pass > 0 && (mflags & M_WAITOK)) {
				for (q = 0; q < PQ_L2_SIZE; ++q) {
					vm_contig_pg_clean(PQ_ACTIVE + q,
						       vmstats.v_active_count);
				}
				lwkt_yield();
			}

			/*
			 * We're already too high in the address space
			 * to succeed, reset to 0 for the next iteration.
			 */
			start = 0;
			continue;	/* next pass */
		}
		start = i;

		/*
		 * Check successive pages for contiguous and free.
		 *
		 * (still in critical section)
		 */
		for (i = start + 1; i < (start + size / PAGE_SIZE); i++) {
			m = &pga[i];
			pqtype = m->queue - m->pc;
			if ((VM_PAGE_TO_PHYS(&m[0]) !=
			    (VM_PAGE_TO_PHYS(&m[-1]) + PAGE_SIZE)) ||
			    ((pqtype != PQ_FREE) && (pqtype != PQ_CACHE)) ||
			    m->busy || m->wire_count ||
			    m->hold_count ||
			    (m->flags & (PG_BUSY | PG_NEED_COMMIT)))
			{
				start++;
				goto again;
			}
		}

		/*
		 * Try to allocate the pages, wiring them as we go.
		 *
		 * (still in critical section)
		 */
		for (i = start; i < (start + size / PAGE_SIZE); i++) {
			m = &pga[i];

			if (vm_page_busy_try(m, TRUE)) {
				vm_contig_pg_free(start,
						  (i - start) * PAGE_SIZE);
				start++;
				goto again;
			}
			pqtype = m->queue - m->pc;
			if (pqtype == PQ_CACHE &&
			    m->hold_count == 0 &&
			    m->wire_count == 0 &&
			    (m->flags & (PG_UNMANAGED | PG_NEED_COMMIT)) == 0) {
				vm_page_protect(m, VM_PROT_NONE);
				KKASSERT((m->flags & PG_MAPPED) == 0);
				KKASSERT(m->dirty == 0);
				vm_page_free(m);
				--i;
				continue;	/* retry the page */
			}
			if (pqtype != PQ_FREE || m->hold_count) {
				vm_page_wakeup(m);
				vm_contig_pg_free(start,
						  (i - start) * PAGE_SIZE);
				start++;
				goto again;
			}
			KKASSERT((m->valid & m->dirty) == 0);
			KKASSERT(m->wire_count == 0);
			KKASSERT(m->object == NULL);
			vm_page_unqueue_nowakeup(m);
			m->valid = VM_PAGE_BITS_ALL;
			if (m->flags & PG_ZERO)
				vm_page_zero_count--;
			KASSERT(m->dirty == 0,
				("vm_contig_pg_alloc: page %p was dirty", m));
			KKASSERT(m->wire_count == 0);
			KKASSERT(m->busy == 0);

			/*
			 * Clear all flags except PG_BUSY, PG_ZERO, and
			 * PG_WANTED, then unbusy the now allocated page.
			 */
			vm_page_flag_clear(m, ~(PG_BUSY | PG_SBUSY |
						PG_ZERO | PG_WANTED));
			vm_page_wire(m);
			vm_page_wakeup(m);
		}

		/*
		 * Our job is done, return the index page of vm_page_array.
		 */
		return (start); /* aka &pga[start] */
	}

	/*
	 * Failed.
	 */
	return (-1);
}

/*
 * vm_contig_pg_free:
 *
 * Remove pages previously allocated by vm_contig_pg_alloc, and
 * assume all references to the pages have been removed, and that
 * it is OK to add them back to the free list.
 *
 * Caller must ensure no races on the page range in question.
 * No other requirements.
 */
static void
vm_contig_pg_free(int start, u_long size)
{
	vm_page_t pga = vm_page_array;
	
	size = round_page(size);
	if (size == 0)
		panic("vm_contig_pg_free: size must not be 0");

	/*
	 * The pages are wired, vm_page_free_contig() determines whether they
	 * belong to the contig space or not and either frees them to that
	 * space (leaving them wired), or unwires the page and frees it to the
	 * normal PQ_FREE queue.
	 */
	vm_page_free_contig(&pga[start], size);
}

/*
 * vm_contig_pg_kmap:
 *
 * Map previously allocated (vm_contig_pg_alloc) range of pages from
 * vm_page_array[] into the KVA.  Once mapped, the pages are part of
 * the Kernel, and are to free'ed with kmem_free(&kernel_map, addr, size).
 *
 * No requirements.
 */
static vm_offset_t
vm_contig_pg_kmap(int start, u_long size, vm_map_t map, int flags)
{
	vm_offset_t addr;
	vm_paddr_t pa;
	vm_page_t pga = vm_page_array;
	u_long offset;

	if (size == 0)
		panic("vm_contig_pg_kmap: size must not be 0");
	size = round_page(size);
	addr = kmem_alloc_pageable(&kernel_map, size);
	if (addr) {
		pa = VM_PAGE_TO_PHYS(&pga[start]);
		for (offset = 0; offset < size; offset += PAGE_SIZE)
			pmap_kenter_quick(addr + offset, pa + offset);
		smp_invltlb();
		if (flags & M_ZERO)
			bzero((void *)addr, size);
	}
	return(addr);
}

/*
 * No requirements.
 */
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
			boundary, &kernel_map);
}

/*
 * No requirements.
 */
void *
contigmalloc_map(unsigned long size, struct malloc_type *type,
		 int flags, vm_paddr_t low, vm_paddr_t high,
		 unsigned long alignment, unsigned long boundary,
		 vm_map_t map)
{
	int index;
	void *rv;

	index = vm_contig_pg_alloc(size, low, high, alignment, boundary, flags);
	if (index < 0) {
		kprintf("contigmalloc_map: failed size %lu low=%llx "
			"high=%llx align=%lu boundary=%lu flags=%08x\n",
			size, (long long)low, (long long)high,
			alignment, boundary, flags);
		return NULL;
	}

	rv = (void *)vm_contig_pg_kmap(index, size, map, flags);
	if (rv == NULL)
		vm_contig_pg_free(index, size);
	
	return rv;
}

/*
 * No requirements.
 */
void
contigfree(void *addr, unsigned long size, struct malloc_type *type)
{
	vm_paddr_t pa;
	vm_page_t m;

	if (size == 0)
		panic("vm_contig_pg_kmap: size must not be 0");
	size = round_page(size);

	pa = pmap_extract(&kernel_pmap, (vm_offset_t)addr);
	pmap_qremove((vm_offset_t)addr, size / PAGE_SIZE);
	kmem_free(&kernel_map, (vm_offset_t)addr, size);

	m = PHYS_TO_VM_PAGE(pa);
	vm_page_free_contig(m, size);
}

/*
 * No requirements.
 */
vm_offset_t
kmem_alloc_contig(vm_offset_t size, vm_paddr_t low, vm_paddr_t high,
		  vm_offset_t alignment)
{
	return ((vm_offset_t)contigmalloc_map(size, M_DEVBUF, M_NOWAIT, low,
				high, alignment, 0ul, &kernel_map));
}
