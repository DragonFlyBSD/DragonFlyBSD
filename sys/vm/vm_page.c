/*
 * (MPSAFE)
 *
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
 * $FreeBSD: src/sys/vm/vm_page.c,v 1.147.2.18 2002/03/10 05:03:19 alc Exp $
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
 * Resident memory management module.  The module manipulates 'VM pages'.
 * A VM page is the core building block for memory management.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>
#include <sys/kernel.h>
#include <sys/alist.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/swap_pager.h>

#include <machine/inttypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#include <vm/vm_page2.h>
#include <sys/spinlock2.h>

#define VMACTION_HSIZE	256
#define VMACTION_HMASK	(VMACTION_HSIZE - 1)

static void vm_page_queue_init(void);
static void vm_page_free_wakeup(void);
static vm_page_t vm_page_select_cache(u_short pg_color);
static vm_page_t _vm_page_list_find2(int basequeue, int index);
static void _vm_page_deactivate_locked(vm_page_t m, int athead);

/*
 * Array of tailq lists
 */
__cachealign struct vpgqueues vm_page_queues[PQ_COUNT];

LIST_HEAD(vm_page_action_list, vm_page_action);
struct vm_page_action_list	action_list[VMACTION_HSIZE];
static volatile int vm_pages_waiting;

static struct alist vm_contig_alist;
static struct almeta vm_contig_ameta[ALIST_RECORDS_65536];
static struct spinlock vm_contig_spin = SPINLOCK_INITIALIZER(&vm_contig_spin, "vm_contig_spin");

static u_long vm_dma_reserved = 0;
TUNABLE_ULONG("vm.dma_reserved", &vm_dma_reserved);
SYSCTL_ULONG(_vm, OID_AUTO, dma_reserved, CTLFLAG_RD, &vm_dma_reserved, 0,
	    "Memory reserved for DMA");
SYSCTL_UINT(_vm, OID_AUTO, dma_free_pages, CTLFLAG_RD,
	    &vm_contig_alist.bl_free, 0, "Memory reserved for DMA");

static int vm_contig_verbose = 0;
TUNABLE_INT("vm.contig_verbose", &vm_contig_verbose);

RB_GENERATE2(vm_page_rb_tree, vm_page, rb_entry, rb_vm_page_compare,
	     vm_pindex_t, pindex);

static void
vm_page_queue_init(void) 
{
	int i;

	for (i = 0; i < PQ_L2_SIZE; i++)
		vm_page_queues[PQ_FREE+i].cnt = &vmstats.v_free_count;
	for (i = 0; i < PQ_L2_SIZE; i++)
		vm_page_queues[PQ_CACHE+i].cnt = &vmstats.v_cache_count;
	for (i = 0; i < PQ_L2_SIZE; i++)
		vm_page_queues[PQ_INACTIVE+i].cnt = &vmstats.v_inactive_count;
	for (i = 0; i < PQ_L2_SIZE; i++)
		vm_page_queues[PQ_ACTIVE+i].cnt = &vmstats.v_active_count;
	for (i = 0; i < PQ_L2_SIZE; i++)
		vm_page_queues[PQ_HOLD+i].cnt = &vmstats.v_active_count;
	/* PQ_NONE has no queue */

	for (i = 0; i < PQ_COUNT; i++) {
		TAILQ_INIT(&vm_page_queues[i].pl);
		spin_init(&vm_page_queues[i].spin, "vm_page_queue_init");
	}

	for (i = 0; i < VMACTION_HSIZE; i++)
		LIST_INIT(&action_list[i]);
}

/*
 * note: place in initialized data section?  Is this necessary?
 */
long first_page = 0;
int vm_page_array_size = 0;
int vm_page_zero_count = 0;
vm_page_t vm_page_array = NULL;
vm_paddr_t vm_low_phys_reserved;

/*
 * (low level boot)
 *
 * Sets the page size, perhaps based upon the memory size.
 * Must be called before any use of page-size dependent functions.
 */
void
vm_set_page_size(void)
{
	if (vmstats.v_page_size == 0)
		vmstats.v_page_size = PAGE_SIZE;
	if (((vmstats.v_page_size - 1) & vmstats.v_page_size) != 0)
		panic("vm_set_page_size: page size not a power of two");
}

/*
 * (low level boot)
 *
 * Add a new page to the freelist for use by the system.  New pages
 * are added to both the head and tail of the associated free page
 * queue in a bottom-up fashion, so both zero'd and non-zero'd page
 * requests pull 'recent' adds (higher physical addresses) first.
 *
 * Beware that the page zeroing daemon will also be running soon after
 * boot, moving pages from the head to the tail of the PQ_FREE queues.
 *
 * Must be called in a critical section.
 */
static void
vm_add_new_page(vm_paddr_t pa)
{
	struct vpgqueues *vpq;
	vm_page_t m;

	m = PHYS_TO_VM_PAGE(pa);
	m->phys_addr = pa;
	m->flags = 0;
	m->pc = (pa >> PAGE_SHIFT) & PQ_L2_MASK;
	m->pat_mode = PAT_WRITE_BACK;
	/*
	 * Twist for cpu localization in addition to page coloring, so
	 * different cpus selecting by m->queue get different page colors.
	 */
	m->pc ^= ((pa >> PAGE_SHIFT) / PQ_L2_SIZE) & PQ_L2_MASK;
	m->pc ^= ((pa >> PAGE_SHIFT) / (PQ_L2_SIZE * PQ_L2_SIZE)) & PQ_L2_MASK;
	/*
	 * Reserve a certain number of contiguous low memory pages for
	 * contigmalloc() to use.
	 */
	if (pa < vm_low_phys_reserved) {
		atomic_add_int(&vmstats.v_page_count, 1);
		atomic_add_int(&vmstats.v_dma_pages, 1);
		m->queue = PQ_NONE;
		m->wire_count = 1;
		atomic_add_int(&vmstats.v_wire_count, 1);
		alist_free(&vm_contig_alist, pa >> PAGE_SHIFT, 1);
		return;
	}

	/*
	 * General page
	 */
	m->queue = m->pc + PQ_FREE;
	KKASSERT(m->dirty == 0);

	atomic_add_int(&vmstats.v_page_count, 1);
	atomic_add_int(&vmstats.v_free_count, 1);
	vpq = &vm_page_queues[m->queue];
	if ((vpq->flipflop & 15) == 0) {
		pmap_zero_page(VM_PAGE_TO_PHYS(m));
		m->flags |= PG_ZERO;
		TAILQ_INSERT_TAIL(&vpq->pl, m, pageq);
		atomic_add_int(&vm_page_zero_count, 1);
	} else {
		TAILQ_INSERT_HEAD(&vpq->pl, m, pageq);
	}
	++vpq->flipflop;
	++vpq->lcnt;
}

/*
 * (low level boot)
 *
 * Initializes the resident memory module.
 *
 * Preallocates memory for critical VM structures and arrays prior to
 * kernel_map becoming available.
 *
 * Memory is allocated from (virtual2_start, virtual2_end) if available,
 * otherwise memory is allocated from (virtual_start, virtual_end).
 *
 * On x86-64 (virtual_start, virtual_end) is only 2GB and may not be
 * large enough to hold vm_page_array & other structures for machines with
 * large amounts of ram, so we want to use virtual2* when available.
 */
void
vm_page_startup(void)
{
	vm_offset_t vaddr = virtual2_start ? virtual2_start : virtual_start;
	vm_offset_t mapped;
	vm_size_t npages;
	vm_paddr_t page_range;
	vm_paddr_t new_end;
	int i;
	vm_paddr_t pa;
	int nblocks;
	vm_paddr_t last_pa;
	vm_paddr_t end;
	vm_paddr_t biggestone, biggestsize;
	vm_paddr_t total;

	total = 0;
	biggestsize = 0;
	biggestone = 0;
	nblocks = 0;
	vaddr = round_page(vaddr);

	for (i = 0; phys_avail[i + 1]; i += 2) {
		phys_avail[i] = round_page64(phys_avail[i]);
		phys_avail[i + 1] = trunc_page64(phys_avail[i + 1]);
	}

	for (i = 0; phys_avail[i + 1]; i += 2) {
		vm_paddr_t size = phys_avail[i + 1] - phys_avail[i];

		if (size > biggestsize) {
			biggestone = i;
			biggestsize = size;
		}
		++nblocks;
		total += size;
	}

	end = phys_avail[biggestone+1];
	end = trunc_page(end);

	/*
	 * Initialize the queue headers for the free queue, the active queue
	 * and the inactive queue.
	 */
	vm_page_queue_init();

#if !defined(_KERNEL_VIRTUAL)
	/*
	 * VKERNELs don't support minidumps and as such don't need
	 * vm_page_dump
	 *
	 * Allocate a bitmap to indicate that a random physical page
	 * needs to be included in a minidump.
	 *
	 * The amd64 port needs this to indicate which direct map pages
	 * need to be dumped, via calls to dump_add_page()/dump_drop_page().
	 *
	 * However, i386 still needs this workspace internally within the
	 * minidump code.  In theory, they are not needed on i386, but are
	 * included should the sf_buf code decide to use them.
	 */
	page_range = phys_avail[(nblocks - 1) * 2 + 1] / PAGE_SIZE;
	vm_page_dump_size = round_page(roundup2(page_range, NBBY) / NBBY);
	end -= vm_page_dump_size;
	vm_page_dump = (void *)pmap_map(&vaddr, end, end + vm_page_dump_size,
	    VM_PROT_READ | VM_PROT_WRITE);
	bzero((void *)vm_page_dump, vm_page_dump_size);
#endif
	/*
	 * Compute the number of pages of memory that will be available for
	 * use (taking into account the overhead of a page structure per
	 * page).
	 */
	first_page = phys_avail[0] / PAGE_SIZE;
	page_range = phys_avail[(nblocks - 1) * 2 + 1] / PAGE_SIZE - first_page;
	npages = (total - (page_range * sizeof(struct vm_page))) / PAGE_SIZE;

#ifndef _KERNEL_VIRTUAL
	/*
	 * (only applies to real kernels)
	 *
	 * Reserve a large amount of low memory for potential 32-bit DMA
	 * space allocations.  Once device initialization is complete we
	 * release most of it, but keep (vm_dma_reserved) memory reserved
	 * for later use.  Typically for X / graphics.  Through trial and
	 * error we find that GPUs usually requires ~60-100MB or so.
	 *
	 * By default, 128M is left in reserve on machines with 2G+ of ram.
	 */
	vm_low_phys_reserved = (vm_paddr_t)65536 << PAGE_SHIFT;
	if (vm_low_phys_reserved > total / 4)
		vm_low_phys_reserved = total / 4;
	if (vm_dma_reserved == 0) {
		vm_dma_reserved = 128 * 1024 * 1024;	/* 128MB */
		if (vm_dma_reserved > total / 16)
			vm_dma_reserved = total / 16;
	}
#endif
	alist_init(&vm_contig_alist, 65536, vm_contig_ameta,
		   ALIST_RECORDS_65536);

	/*
	 * Initialize the mem entry structures now, and put them in the free
	 * queue.
	 */
	new_end = trunc_page(end - page_range * sizeof(struct vm_page));
	mapped = pmap_map(&vaddr, new_end, end, VM_PROT_READ | VM_PROT_WRITE);
	vm_page_array = (vm_page_t)mapped;

#if defined(__x86_64__) && !defined(_KERNEL_VIRTUAL)
	/*
	 * since pmap_map on amd64 returns stuff out of a direct-map region,
	 * we have to manually add these pages to the minidump tracking so
	 * that they can be dumped, including the vm_page_array.
	 */
	for (pa = new_end; pa < phys_avail[biggestone + 1]; pa += PAGE_SIZE)
		dump_add_page(pa);
#endif

	/*
	 * Clear all of the page structures
	 */
	bzero((caddr_t) vm_page_array, page_range * sizeof(struct vm_page));
	vm_page_array_size = page_range;

	/*
	 * Construct the free queue(s) in ascending order (by physical
	 * address) so that the first 16MB of physical memory is allocated
	 * last rather than first.  On large-memory machines, this avoids
	 * the exhaustion of low physical memory before isa_dmainit has run.
	 */
	vmstats.v_page_count = 0;
	vmstats.v_free_count = 0;
	for (i = 0; phys_avail[i + 1] && npages > 0; i += 2) {
		pa = phys_avail[i];
		if (i == biggestone)
			last_pa = new_end;
		else
			last_pa = phys_avail[i + 1];
		while (pa < last_pa && npages-- > 0) {
			vm_add_new_page(pa);
			pa += PAGE_SIZE;
		}
	}
	if (virtual2_start)
		virtual2_start = vaddr;
	else
		virtual_start = vaddr;
}

/*
 * We tended to reserve a ton of memory for contigmalloc().  Now that most
 * drivers have initialized we want to return most the remaining free
 * reserve back to the VM page queues so they can be used for normal
 * allocations.
 *
 * We leave vm_dma_reserved bytes worth of free pages in the reserve pool.
 */
static void
vm_page_startup_finish(void *dummy __unused)
{
	alist_blk_t blk;
	alist_blk_t rblk;
	alist_blk_t count;
	alist_blk_t xcount;
	alist_blk_t bfree;
	vm_page_t m;

	spin_lock(&vm_contig_spin);
	for (;;) {
		bfree = alist_free_info(&vm_contig_alist, &blk, &count);
		if (bfree <= vm_dma_reserved / PAGE_SIZE)
			break;
		if (count == 0)
			break;

		/*
		 * Figure out how much of the initial reserve we have to
		 * free in order to reach our target.
		 */
		bfree -= vm_dma_reserved / PAGE_SIZE;
		if (count > bfree) {
			blk += count - bfree;
			count = bfree;
		}

		/*
		 * Calculate the nearest power of 2 <= count.
		 */
		for (xcount = 1; xcount <= count; xcount <<= 1)
			;
		xcount >>= 1;
		blk += count - xcount;
		count = xcount;

		/*
		 * Allocate the pages from the alist, then free them to
		 * the normal VM page queues.
		 *
		 * Pages allocated from the alist are wired.  We have to
		 * busy, unwire, and free them.  We must also adjust
		 * vm_low_phys_reserved before freeing any pages to prevent
		 * confusion.
		 */
		rblk = alist_alloc(&vm_contig_alist, blk, count);
		if (rblk != blk) {
			kprintf("vm_page_startup_finish: Unable to return "
				"dma space @0x%08x/%d -> 0x%08x\n",
				blk, count, rblk);
			break;
		}
		atomic_add_int(&vmstats.v_dma_pages, -count);
		spin_unlock(&vm_contig_spin);

		m = PHYS_TO_VM_PAGE((vm_paddr_t)blk << PAGE_SHIFT);
		vm_low_phys_reserved = VM_PAGE_TO_PHYS(m);
		while (count) {
			vm_page_busy_wait(m, FALSE, "cpgfr");
			vm_page_unwire(m, 0);
			vm_page_free(m);
			--count;
			++m;
		}
		spin_lock(&vm_contig_spin);
	}
	spin_unlock(&vm_contig_spin);

	/*
	 * Print out how much DMA space drivers have already allocated and
	 * how much is left over.
	 */
	kprintf("DMA space used: %jdk, remaining available: %jdk\n",
		(intmax_t)(vmstats.v_dma_pages - vm_contig_alist.bl_free) *
		(PAGE_SIZE / 1024),
		(intmax_t)vm_contig_alist.bl_free * (PAGE_SIZE / 1024));
}
SYSINIT(vm_pgend, SI_SUB_PROC0_POST, SI_ORDER_ANY,
	vm_page_startup_finish, NULL);


/*
 * Scan comparison function for Red-Black tree scans.  An inclusive
 * (start,end) is expected.  Other fields are not used.
 */
int
rb_vm_page_scancmp(struct vm_page *p, void *data)
{
	struct rb_vm_page_scan_info *info = data;

	if (p->pindex < info->start_pindex)
		return(-1);
	if (p->pindex > info->end_pindex)
		return(1);
	return(0);
}

int
rb_vm_page_compare(struct vm_page *p1, struct vm_page *p2)
{
	if (p1->pindex < p2->pindex)
		return(-1);
	if (p1->pindex > p2->pindex)
		return(1);
	return(0);
}

void
vm_page_init(vm_page_t m)
{
	/* do nothing for now.  Called from pmap_page_init() */
}

/*
 * Each page queue has its own spin lock, which is fairly optimal for
 * allocating and freeing pages at least.
 *
 * The caller must hold the vm_page_spin_lock() before locking a vm_page's
 * queue spinlock via this function.  Also note that m->queue cannot change
 * unless both the page and queue are locked.
 */
static __inline
void
_vm_page_queue_spin_lock(vm_page_t m)
{
	u_short queue;

	queue = m->queue;
	if (queue != PQ_NONE) {
		spin_lock(&vm_page_queues[queue].spin);
		KKASSERT(queue == m->queue);
	}
}

static __inline
void
_vm_page_queue_spin_unlock(vm_page_t m)
{
	u_short queue;

	queue = m->queue;
	cpu_ccfence();
	if (queue != PQ_NONE)
		spin_unlock(&vm_page_queues[queue].spin);
}

static __inline
void
_vm_page_queues_spin_lock(u_short queue)
{
	cpu_ccfence();
	if (queue != PQ_NONE)
		spin_lock(&vm_page_queues[queue].spin);
}


static __inline
void
_vm_page_queues_spin_unlock(u_short queue)
{
	cpu_ccfence();
	if (queue != PQ_NONE)
		spin_unlock(&vm_page_queues[queue].spin);
}

void
vm_page_queue_spin_lock(vm_page_t m)
{
	_vm_page_queue_spin_lock(m);
}

void
vm_page_queues_spin_lock(u_short queue)
{
	_vm_page_queues_spin_lock(queue);
}

void
vm_page_queue_spin_unlock(vm_page_t m)
{
	_vm_page_queue_spin_unlock(m);
}

void
vm_page_queues_spin_unlock(u_short queue)
{
	_vm_page_queues_spin_unlock(queue);
}

/*
 * This locks the specified vm_page and its queue in the proper order
 * (page first, then queue).  The queue may change so the caller must
 * recheck on return.
 */
static __inline
void
_vm_page_and_queue_spin_lock(vm_page_t m)
{
	vm_page_spin_lock(m);
	_vm_page_queue_spin_lock(m);
}

static __inline
void
_vm_page_and_queue_spin_unlock(vm_page_t m)
{
	_vm_page_queues_spin_unlock(m->queue);
	vm_page_spin_unlock(m);
}

void
vm_page_and_queue_spin_unlock(vm_page_t m)
{
	_vm_page_and_queue_spin_unlock(m);
}

void
vm_page_and_queue_spin_lock(vm_page_t m)
{
	_vm_page_and_queue_spin_lock(m);
}

/*
 * Helper function removes vm_page from its current queue.
 * Returns the base queue the page used to be on.
 *
 * The vm_page and the queue must be spinlocked.
 * This function will unlock the queue but leave the page spinlocked.
 */
static __inline u_short
_vm_page_rem_queue_spinlocked(vm_page_t m)
{
	struct vpgqueues *pq;
	u_short queue;

	queue = m->queue;
	if (queue != PQ_NONE) {
		pq = &vm_page_queues[queue];
		TAILQ_REMOVE(&pq->pl, m, pageq);
		atomic_add_int(pq->cnt, -1);
		pq->lcnt--;
		m->queue = PQ_NONE;
		vm_page_queues_spin_unlock(queue);
		if ((queue - m->pc) == PQ_FREE && (m->flags & PG_ZERO))
			atomic_subtract_int(&vm_page_zero_count, 1);
		if ((queue - m->pc) == PQ_CACHE || (queue - m->pc) == PQ_FREE)
			return (queue - m->pc);
	}
	return queue;
}

/*
 * Helper function places the vm_page on the specified queue.
 *
 * The vm_page must be spinlocked.
 * This function will return with both the page and the queue locked.
 */
static __inline void
_vm_page_add_queue_spinlocked(vm_page_t m, u_short queue, int athead)
{
	struct vpgqueues *pq;

	KKASSERT(m->queue == PQ_NONE);

	if (queue != PQ_NONE) {
		vm_page_queues_spin_lock(queue);
		pq = &vm_page_queues[queue];
		++pq->lcnt;
		atomic_add_int(pq->cnt, 1);
		m->queue = queue;

		/*
		 * Put zero'd pages on the end ( where we look for zero'd pages
		 * first ) and non-zerod pages at the head.
		 */
		if (queue - m->pc == PQ_FREE) {
			if (m->flags & PG_ZERO) {
				TAILQ_INSERT_TAIL(&pq->pl, m, pageq);
				atomic_add_int(&vm_page_zero_count, 1);
			} else {
				TAILQ_INSERT_HEAD(&pq->pl, m, pageq);
			}
		} else if (athead) {
			TAILQ_INSERT_HEAD(&pq->pl, m, pageq);
		} else {
			TAILQ_INSERT_TAIL(&pq->pl, m, pageq);
		}
		/* leave the queue spinlocked */
	}
}

/*
 * Wait until page is no longer PG_BUSY or (if also_m_busy is TRUE)
 * m->busy is zero.  Returns TRUE if it had to sleep, FALSE if we
 * did not.  Only one sleep call will be made before returning.
 *
 * This function does NOT busy the page and on return the page is not
 * guaranteed to be available.
 */
void
vm_page_sleep_busy(vm_page_t m, int also_m_busy, const char *msg)
{
	u_int32_t flags;

	for (;;) {
		flags = m->flags;
		cpu_ccfence();

		if ((flags & PG_BUSY) == 0 &&
		    (also_m_busy == 0 || (flags & PG_SBUSY) == 0)) {
			break;
		}
		tsleep_interlock(m, 0);
		if (atomic_cmpset_int(&m->flags, flags,
				      flags | PG_WANTED | PG_REFERENCED)) {
			tsleep(m, PINTERLOCKED, msg, 0);
			break;
		}
	}
}

/*
 * Wait until PG_BUSY can be set, then set it.  If also_m_busy is TRUE we
 * also wait for m->busy to become 0 before setting PG_BUSY.
 */
void
VM_PAGE_DEBUG_EXT(vm_page_busy_wait)(vm_page_t m,
				     int also_m_busy, const char *msg
				     VM_PAGE_DEBUG_ARGS)
{
	u_int32_t flags;

	for (;;) {
		flags = m->flags;
		cpu_ccfence();
		if (flags & PG_BUSY) {
			tsleep_interlock(m, 0);
			if (atomic_cmpset_int(&m->flags, flags,
					  flags | PG_WANTED | PG_REFERENCED)) {
				tsleep(m, PINTERLOCKED, msg, 0);
			}
		} else if (also_m_busy && (flags & PG_SBUSY)) {
			tsleep_interlock(m, 0);
			if (atomic_cmpset_int(&m->flags, flags,
					  flags | PG_WANTED | PG_REFERENCED)) {
				tsleep(m, PINTERLOCKED, msg, 0);
			}
		} else {
			if (atomic_cmpset_int(&m->flags, flags,
					      flags | PG_BUSY)) {
#ifdef VM_PAGE_DEBUG
				m->busy_func = func;
				m->busy_line = lineno;
#endif
				break;
			}
		}
	}
}

/*
 * Attempt to set PG_BUSY.  If also_m_busy is TRUE we only succeed if m->busy
 * is also 0.
 *
 * Returns non-zero on failure.
 */
int
VM_PAGE_DEBUG_EXT(vm_page_busy_try)(vm_page_t m, int also_m_busy
				    VM_PAGE_DEBUG_ARGS)
{
	u_int32_t flags;

	for (;;) {
		flags = m->flags;
		cpu_ccfence();
		if (flags & PG_BUSY)
			return TRUE;
		if (also_m_busy && (flags & PG_SBUSY))
			return TRUE;
		if (atomic_cmpset_int(&m->flags, flags, flags | PG_BUSY)) {
#ifdef VM_PAGE_DEBUG
				m->busy_func = func;
				m->busy_line = lineno;
#endif
			return FALSE;
		}
	}
}

/*
 * Clear the PG_BUSY flag and return non-zero to indicate to the caller
 * that a wakeup() should be performed.
 *
 * The vm_page must be spinlocked and will remain spinlocked on return.
 * The related queue must NOT be spinlocked (which could deadlock us).
 *
 * (inline version)
 */
static __inline
int
_vm_page_wakeup(vm_page_t m)
{
	u_int32_t flags;

	for (;;) {
		flags = m->flags;
		cpu_ccfence();
		if (atomic_cmpset_int(&m->flags, flags,
				      flags & ~(PG_BUSY | PG_WANTED))) {
			break;
		}
	}
	return(flags & PG_WANTED);
}

/*
 * Clear the PG_BUSY flag and wakeup anyone waiting for the page.  This
 * is typically the last call you make on a page before moving onto
 * other things.
 */
void
vm_page_wakeup(vm_page_t m)
{
        KASSERT(m->flags & PG_BUSY, ("vm_page_wakeup: page not busy!!!"));
	vm_page_spin_lock(m);
	if (_vm_page_wakeup(m)) {
		vm_page_spin_unlock(m);
		wakeup(m);
	} else {
		vm_page_spin_unlock(m);
	}
}

/*
 * Holding a page keeps it from being reused.  Other parts of the system
 * can still disassociate the page from its current object and free it, or
 * perform read or write I/O on it and/or otherwise manipulate the page,
 * but if the page is held the VM system will leave the page and its data
 * intact and not reuse the page for other purposes until the last hold
 * reference is released.  (see vm_page_wire() if you want to prevent the
 * page from being disassociated from its object too).
 *
 * The caller must still validate the contents of the page and, if necessary,
 * wait for any pending I/O (e.g. vm_page_sleep_busy() loop) to complete
 * before manipulating the page.
 *
 * XXX get vm_page_spin_lock() here and move FREE->HOLD if necessary
 */
void
vm_page_hold(vm_page_t m)
{
	vm_page_spin_lock(m);
	atomic_add_int(&m->hold_count, 1);
	if (m->queue - m->pc == PQ_FREE) {
		_vm_page_queue_spin_lock(m);
		_vm_page_rem_queue_spinlocked(m);
		_vm_page_add_queue_spinlocked(m, PQ_HOLD + m->pc, 0);
		_vm_page_queue_spin_unlock(m);
	}
	vm_page_spin_unlock(m);
}

/*
 * The opposite of vm_page_hold().  If the page is on the HOLD queue
 * it was freed while held and must be moved back to the FREE queue.
 */
void
vm_page_unhold(vm_page_t m)
{
	KASSERT(m->hold_count > 0 && m->queue - m->pc != PQ_FREE,
		("vm_page_unhold: pg %p illegal hold_count (%d) or on FREE queue (%d)",
		 m, m->hold_count, m->queue - m->pc));
	vm_page_spin_lock(m);
	atomic_add_int(&m->hold_count, -1);
	if (m->hold_count == 0 && m->queue - m->pc == PQ_HOLD) {
		_vm_page_queue_spin_lock(m);
		_vm_page_rem_queue_spinlocked(m);
		_vm_page_add_queue_spinlocked(m, PQ_FREE + m->pc, 0);
		_vm_page_queue_spin_unlock(m);
	}
	vm_page_spin_unlock(m);
}

/*
 *	vm_page_getfake:
 *
 *	Create a fictitious page with the specified physical address and
 *	memory attribute.  The memory attribute is the only the machine-
 *	dependent aspect of a fictitious page that must be initialized.
 */

void
vm_page_initfake(vm_page_t m, vm_paddr_t paddr, vm_memattr_t memattr)
{

	if ((m->flags & PG_FICTITIOUS) != 0) {
		/*
		 * The page's memattr might have changed since the
		 * previous initialization.  Update the pmap to the
		 * new memattr.
		 */
		goto memattr;
	}
	m->phys_addr = paddr;
	m->queue = PQ_NONE;
	/* Fictitious pages don't use "segind". */
	/* Fictitious pages don't use "order" or "pool". */
	m->flags = PG_FICTITIOUS | PG_UNMANAGED | PG_BUSY;
	m->wire_count = 1;
	pmap_page_init(m);
memattr:
	pmap_page_set_memattr(m, memattr);
}

/*
 * Inserts the given vm_page into the object and object list.
 *
 * The pagetables are not updated but will presumably fault the page
 * in if necessary, or if a kernel page the caller will at some point
 * enter the page into the kernel's pmap.  We are not allowed to block
 * here so we *can't* do this anyway.
 *
 * This routine may not block.
 * This routine must be called with the vm_object held.
 * This routine must be called with a critical section held.
 *
 * This routine returns TRUE if the page was inserted into the object
 * successfully, and FALSE if the page already exists in the object.
 */
int
vm_page_insert(vm_page_t m, vm_object_t object, vm_pindex_t pindex)
{
	ASSERT_LWKT_TOKEN_HELD_EXCL(vm_object_token(object));
	if (m->object != NULL)
		panic("vm_page_insert: already inserted");

	object->generation++;

	/*
	 * Record the object/offset pair in this page and add the
	 * pv_list_count of the page to the object.
	 *
	 * The vm_page spin lock is required for interactions with the pmap.
	 */
	vm_page_spin_lock(m);
	m->object = object;
	m->pindex = pindex;
	if (vm_page_rb_tree_RB_INSERT(&object->rb_memq, m)) {
		m->object = NULL;
		m->pindex = 0;
		vm_page_spin_unlock(m);
		return FALSE;
	}
	++object->resident_page_count;
	++mycpu->gd_vmtotal.t_rm;
	/* atomic_add_int(&object->agg_pv_list_count, m->md.pv_list_count); */
	vm_page_spin_unlock(m);

	/*
	 * Since we are inserting a new and possibly dirty page,
	 * update the object's OBJ_WRITEABLE and OBJ_MIGHTBEDIRTY flags.
	 */
	if ((m->valid & m->dirty) ||
	    (m->flags & (PG_WRITEABLE | PG_NEED_COMMIT)))
		vm_object_set_writeable_dirty(object);

	/*
	 * Checks for a swap assignment and sets PG_SWAPPED if appropriate.
	 */
	swap_pager_page_inserted(m);
	return TRUE;
}

/*
 * Removes the given vm_page_t from the (object,index) table
 *
 * The underlying pmap entry (if any) is NOT removed here.
 * This routine may not block.
 *
 * The page must be BUSY and will remain BUSY on return.
 * No other requirements.
 *
 * NOTE: FreeBSD side effect was to unbusy the page on return.  We leave
 *	 it busy.
 */
void
vm_page_remove(vm_page_t m)
{
	vm_object_t object;

	if (m->object == NULL) {
		return;
	}

	if ((m->flags & PG_BUSY) == 0)
		panic("vm_page_remove: page not busy");

	object = m->object;

	vm_object_hold(object);

	/*
	 * Remove the page from the object and update the object.
	 *
	 * The vm_page spin lock is required for interactions with the pmap.
	 */
	vm_page_spin_lock(m);
	vm_page_rb_tree_RB_REMOVE(&object->rb_memq, m);
	--object->resident_page_count;
	--mycpu->gd_vmtotal.t_rm;
	/* atomic_add_int(&object->agg_pv_list_count, -m->md.pv_list_count); */
	m->object = NULL;
	vm_page_spin_unlock(m);

	object->generation++;

	vm_object_drop(object);
}

/*
 * Locate and return the page at (object, pindex), or NULL if the
 * page could not be found.
 *
 * The caller must hold the vm_object token.
 */
vm_page_t
vm_page_lookup(vm_object_t object, vm_pindex_t pindex)
{
	vm_page_t m;

	/*
	 * Search the hash table for this object/offset pair
	 */
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	m = vm_page_rb_tree_RB_LOOKUP(&object->rb_memq, pindex);
	KKASSERT(m == NULL || (m->object == object && m->pindex == pindex));
	return(m);
}

vm_page_t
VM_PAGE_DEBUG_EXT(vm_page_lookup_busy_wait)(struct vm_object *object,
					    vm_pindex_t pindex,
					    int also_m_busy, const char *msg
					    VM_PAGE_DEBUG_ARGS)
{
	u_int32_t flags;
	vm_page_t m;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	m = vm_page_rb_tree_RB_LOOKUP(&object->rb_memq, pindex);
	while (m) {
		KKASSERT(m->object == object && m->pindex == pindex);
		flags = m->flags;
		cpu_ccfence();
		if (flags & PG_BUSY) {
			tsleep_interlock(m, 0);
			if (atomic_cmpset_int(&m->flags, flags,
					  flags | PG_WANTED | PG_REFERENCED)) {
				tsleep(m, PINTERLOCKED, msg, 0);
				m = vm_page_rb_tree_RB_LOOKUP(&object->rb_memq,
							      pindex);
			}
		} else if (also_m_busy && (flags & PG_SBUSY)) {
			tsleep_interlock(m, 0);
			if (atomic_cmpset_int(&m->flags, flags,
					  flags | PG_WANTED | PG_REFERENCED)) {
				tsleep(m, PINTERLOCKED, msg, 0);
				m = vm_page_rb_tree_RB_LOOKUP(&object->rb_memq,
							      pindex);
			}
		} else if (atomic_cmpset_int(&m->flags, flags,
					     flags | PG_BUSY)) {
#ifdef VM_PAGE_DEBUG
			m->busy_func = func;
			m->busy_line = lineno;
#endif
			break;
		}
	}
	return m;
}

/*
 * Attempt to lookup and busy a page.
 *
 * Returns NULL if the page could not be found
 *
 * Returns a vm_page and error == TRUE if the page exists but could not
 * be busied.
 *
 * Returns a vm_page and error == FALSE on success.
 */
vm_page_t
VM_PAGE_DEBUG_EXT(vm_page_lookup_busy_try)(struct vm_object *object,
					   vm_pindex_t pindex,
					   int also_m_busy, int *errorp
					   VM_PAGE_DEBUG_ARGS)
{
	u_int32_t flags;
	vm_page_t m;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	m = vm_page_rb_tree_RB_LOOKUP(&object->rb_memq, pindex);
	*errorp = FALSE;
	while (m) {
		KKASSERT(m->object == object && m->pindex == pindex);
		flags = m->flags;
		cpu_ccfence();
		if (flags & PG_BUSY) {
			*errorp = TRUE;
			break;
		}
		if (also_m_busy && (flags & PG_SBUSY)) {
			*errorp = TRUE;
			break;
		}
		if (atomic_cmpset_int(&m->flags, flags, flags | PG_BUSY)) {
#ifdef VM_PAGE_DEBUG
			m->busy_func = func;
			m->busy_line = lineno;
#endif
			break;
		}
	}
	return m;
}

/*
 * Caller must hold the related vm_object
 */
vm_page_t
vm_page_next(vm_page_t m)
{
	vm_page_t next;

	next = vm_page_rb_tree_RB_NEXT(m);
	if (next && next->pindex != m->pindex + 1)
		next = NULL;
	return (next);
}

/*
 * vm_page_rename()
 *
 * Move the given vm_page from its current object to the specified
 * target object/offset.  The page must be busy and will remain so
 * on return.
 *
 * new_object must be held.
 * This routine might block. XXX ?
 *
 * NOTE: Swap associated with the page must be invalidated by the move.  We
 *       have to do this for several reasons:  (1) we aren't freeing the
 *       page, (2) we are dirtying the page, (3) the VM system is probably
 *       moving the page from object A to B, and will then later move
 *       the backing store from A to B and we can't have a conflict.
 *
 * NOTE: We *always* dirty the page.  It is necessary both for the
 *       fact that we moved it, and because we may be invalidating
 *	 swap.  If the page is on the cache, we have to deactivate it
 *	 or vm_page_dirty() will panic.  Dirty pages are not allowed
 *	 on the cache.
 */
void
vm_page_rename(vm_page_t m, vm_object_t new_object, vm_pindex_t new_pindex)
{
	KKASSERT(m->flags & PG_BUSY);
	ASSERT_LWKT_TOKEN_HELD_EXCL(vm_object_token(new_object));
	if (m->object) {
		ASSERT_LWKT_TOKEN_HELD_EXCL(vm_object_token(m->object));
		vm_page_remove(m);
	}
	if (vm_page_insert(m, new_object, new_pindex) == FALSE) {
		panic("vm_page_rename: target exists (%p,%"PRIu64")",
		      new_object, new_pindex);
	}
	if (m->queue - m->pc == PQ_CACHE)
		vm_page_deactivate(m);
	vm_page_dirty(m);
}

/*
 * vm_page_unqueue() without any wakeup.  This routine is used when a page
 * is to remain BUSYied by the caller.
 *
 * This routine may not block.
 */
void
vm_page_unqueue_nowakeup(vm_page_t m)
{
	vm_page_and_queue_spin_lock(m);
	(void)_vm_page_rem_queue_spinlocked(m);
	vm_page_spin_unlock(m);
}

/*
 * vm_page_unqueue() - Remove a page from its queue, wakeup the pagedemon
 * if necessary.
 *
 * This routine may not block.
 */
void
vm_page_unqueue(vm_page_t m)
{
	u_short queue;

	vm_page_and_queue_spin_lock(m);
	queue = _vm_page_rem_queue_spinlocked(m);
	if (queue == PQ_FREE || queue == PQ_CACHE) {
		vm_page_spin_unlock(m);
		pagedaemon_wakeup();
	} else {
		vm_page_spin_unlock(m);
	}
}

/*
 * vm_page_list_find()
 *
 * Find a page on the specified queue with color optimization.
 *
 * The page coloring optimization attempts to locate a page that does
 * not overload other nearby pages in the object in the cpu's L1 or L2
 * caches.  We need this optimization because cpu caches tend to be
 * physical caches, while object spaces tend to be virtual.
 *
 * On MP systems each PQ_FREE and PQ_CACHE color queue has its own spinlock
 * and the algorithm is adjusted to localize allocations on a per-core basis.
 * This is done by 'twisting' the colors.
 *
 * The page is returned spinlocked and removed from its queue (it will
 * be on PQ_NONE), or NULL. The page is not PG_BUSY'd.  The caller
 * is responsible for dealing with the busy-page case (usually by
 * deactivating the page and looping).
 *
 * NOTE:  This routine is carefully inlined.  A non-inlined version
 *	  is available for outside callers but the only critical path is
 *	  from within this source file.
 *
 * NOTE:  This routine assumes that the vm_pages found in PQ_CACHE and PQ_FREE
 *	  represent stable storage, allowing us to order our locks vm_page
 *	  first, then queue.
 */
static __inline
vm_page_t
_vm_page_list_find(int basequeue, int index, boolean_t prefer_zero)
{
	vm_page_t m;

	for (;;) {
		if (prefer_zero)
			m = TAILQ_LAST(&vm_page_queues[basequeue+index].pl, pglist);
		else
			m = TAILQ_FIRST(&vm_page_queues[basequeue+index].pl);
		if (m == NULL) {
			m = _vm_page_list_find2(basequeue, index);
			return(m);
		}
		vm_page_and_queue_spin_lock(m);
		if (m->queue == basequeue + index) {
			_vm_page_rem_queue_spinlocked(m);
			/* vm_page_t spin held, no queue spin */
			break;
		}
		vm_page_and_queue_spin_unlock(m);
	}
	return(m);
}

static vm_page_t
_vm_page_list_find2(int basequeue, int index)
{
	int i;
	vm_page_t m = NULL;
	struct vpgqueues *pq;

	pq = &vm_page_queues[basequeue];

	/*
	 * Note that for the first loop, index+i and index-i wind up at the
	 * same place.  Even though this is not totally optimal, we've already
	 * blown it by missing the cache case so we do not care.
	 */
	for (i = PQ_L2_SIZE / 2; i > 0; --i) {
		for (;;) {
			m = TAILQ_FIRST(&pq[(index + i) & PQ_L2_MASK].pl);
			if (m) {
				_vm_page_and_queue_spin_lock(m);
				if (m->queue ==
				    basequeue + ((index + i) & PQ_L2_MASK)) {
					_vm_page_rem_queue_spinlocked(m);
					return(m);
				}
				_vm_page_and_queue_spin_unlock(m);
				continue;
			}
			m = TAILQ_FIRST(&pq[(index - i) & PQ_L2_MASK].pl);
			if (m) {
				_vm_page_and_queue_spin_lock(m);
				if (m->queue ==
				    basequeue + ((index - i) & PQ_L2_MASK)) {
					_vm_page_rem_queue_spinlocked(m);
					return(m);
				}
				_vm_page_and_queue_spin_unlock(m);
				continue;
			}
			break;	/* next i */
		}
	}
	return(m);
}

/*
 * Returns a vm_page candidate for allocation.  The page is not busied so
 * it can move around.  The caller must busy the page (and typically
 * deactivate it if it cannot be busied!)
 *
 * Returns a spinlocked vm_page that has been removed from its queue.
 */
vm_page_t
vm_page_list_find(int basequeue, int index, boolean_t prefer_zero)
{
	return(_vm_page_list_find(basequeue, index, prefer_zero));
}

/*
 * Find a page on the cache queue with color optimization, remove it
 * from the queue, and busy it.  The returned page will not be spinlocked.
 *
 * A candidate failure will be deactivated.  Candidates can fail due to
 * being busied by someone else, in which case they will be deactivated.
 *
 * This routine may not block.
 *
 */
static vm_page_t
vm_page_select_cache(u_short pg_color)
{
	vm_page_t m;

	for (;;) {
		m = _vm_page_list_find(PQ_CACHE, pg_color & PQ_L2_MASK, FALSE);
		if (m == NULL)
			break;
		/*
		 * (m) has been removed from its queue and spinlocked
		 */
		if (vm_page_busy_try(m, TRUE)) {
			_vm_page_deactivate_locked(m, 0);
			vm_page_spin_unlock(m);
		} else {
			/*
			 * We successfully busied the page
			 */
			if ((m->flags & (PG_UNMANAGED | PG_NEED_COMMIT)) == 0 &&
			    m->hold_count == 0 &&
			    m->wire_count == 0 &&
			    (m->dirty & m->valid) == 0) {
				vm_page_spin_unlock(m);
				pagedaemon_wakeup();
				return(m);
			}

			/*
			 * The page cannot be recycled, deactivate it.
			 */
			_vm_page_deactivate_locked(m, 0);
			if (_vm_page_wakeup(m)) {
				vm_page_spin_unlock(m);
				wakeup(m);
			} else {
				vm_page_spin_unlock(m);
			}
		}
	}
	return (m);
}

/*
 * Find a free or zero page, with specified preference.  We attempt to
 * inline the nominal case and fall back to _vm_page_select_free() 
 * otherwise.  A busied page is removed from the queue and returned.
 *
 * This routine may not block.
 */
static __inline vm_page_t
vm_page_select_free(u_short pg_color, boolean_t prefer_zero)
{
	vm_page_t m;

	for (;;) {
		m = _vm_page_list_find(PQ_FREE, pg_color & PQ_L2_MASK,
				       prefer_zero);
		if (m == NULL)
			break;
		if (vm_page_busy_try(m, TRUE)) {
			/*
			 * Various mechanisms such as a pmap_collect can
			 * result in a busy page on the free queue.  We
			 * have to move the page out of the way so we can
			 * retry the allocation.  If the other thread is not
			 * allocating the page then m->valid will remain 0 and
			 * the pageout daemon will free the page later on.
			 *
			 * Since we could not busy the page, however, we
			 * cannot make assumptions as to whether the page
			 * will be allocated by the other thread or not,
			 * so all we can do is deactivate it to move it out
			 * of the way.  In particular, if the other thread
			 * wires the page it may wind up on the inactive
			 * queue and the pageout daemon will have to deal
			 * with that case too.
			 */
			_vm_page_deactivate_locked(m, 0);
			vm_page_spin_unlock(m);
		} else {
			/*
			 * Theoretically if we are able to busy the page
			 * atomic with the queue removal (using the vm_page
			 * lock) nobody else should be able to mess with the
			 * page before us.
			 */
			KKASSERT((m->flags & (PG_UNMANAGED |
					      PG_NEED_COMMIT)) == 0);
			KASSERT(m->hold_count == 0, ("m->hold_count is not zero "
						     "pg %p q=%d flags=%08x hold=%d wire=%d",
						     m, m->queue, m->flags, m->hold_count, m->wire_count));
			KKASSERT(m->wire_count == 0);
			vm_page_spin_unlock(m);
			pagedaemon_wakeup();

			/* return busied and removed page */
			return(m);
		}
	}
	return(m);
}

/*
 * This implements a per-cpu cache of free, zero'd, ready-to-go pages.
 * The idea is to populate this cache prior to acquiring any locks so
 * we don't wind up potentially zeroing VM pages (under heavy loads) while
 * holding potentialy contending locks.
 *
 * Note that we allocate the page uninserted into anything and use a pindex
 * of 0, the vm_page_alloc() will effectively add gd_cpuid so these
 * allocations should wind up being uncontended.  However, we still want
 * to rove across PQ_L2_SIZE.
 */
void
vm_page_pcpu_cache(void)
{
#if 0
	globaldata_t gd = mycpu;
	vm_page_t m;

	if (gd->gd_vmpg_count < GD_MINVMPG) {
		crit_enter_gd(gd);
		while (gd->gd_vmpg_count < GD_MAXVMPG) {
			m = vm_page_alloc(NULL, ticks & ~ncpus2_mask,
					  VM_ALLOC_NULL_OK | VM_ALLOC_NORMAL |
					  VM_ALLOC_NULL_OK | VM_ALLOC_ZERO);
			if (gd->gd_vmpg_count < GD_MAXVMPG) {
				if ((m->flags & PG_ZERO) == 0) {
					pmap_zero_page(VM_PAGE_TO_PHYS(m));
					vm_page_flag_set(m, PG_ZERO);
				}
				gd->gd_vmpg_array[gd->gd_vmpg_count++] = m;
			} else {
				vm_page_free(m);
			}
		}
		crit_exit_gd(gd);
	}
#endif
}

/*
 * vm_page_alloc()
 *
 * Allocate and return a memory cell associated with this VM object/offset
 * pair.  If object is NULL an unassociated page will be allocated.
 *
 * The returned page will be busied and removed from its queues.  This
 * routine can block and may return NULL if a race occurs and the page
 * is found to already exist at the specified (object, pindex).
 *
 *	VM_ALLOC_NORMAL		allow use of cache pages, nominal free drain
 *	VM_ALLOC_QUICK		like normal but cannot use cache
 *	VM_ALLOC_SYSTEM		greater free drain
 *	VM_ALLOC_INTERRUPT	allow free list to be completely drained
 *	VM_ALLOC_ZERO		advisory request for pre-zero'd page only
 *	VM_ALLOC_FORCE_ZERO	advisory request for pre-zero'd page only
 *	VM_ALLOC_NULL_OK	ok to return NULL on insertion collision
 *				(see vm_page_grab())
 *	VM_ALLOC_USE_GD		ok to use per-gd cache
 *
 * The object must be held if not NULL
 * This routine may not block
 *
 * Additional special handling is required when called from an interrupt
 * (VM_ALLOC_INTERRUPT).  We are not allowed to mess with the page cache
 * in this case.
 */
vm_page_t
vm_page_alloc(vm_object_t object, vm_pindex_t pindex, int page_req)
{
	globaldata_t gd = mycpu;
	vm_object_t obj;
	vm_page_t m;
	u_short pg_color;

#if 0
	/*
	 * Special per-cpu free VM page cache.  The pages are pre-busied
	 * and pre-zerod for us.
	 */
	if (gd->gd_vmpg_count && (page_req & VM_ALLOC_USE_GD)) {
		crit_enter_gd(gd);
		if (gd->gd_vmpg_count) {
			m = gd->gd_vmpg_array[--gd->gd_vmpg_count];
			crit_exit_gd(gd);
			goto done;
                }
		crit_exit_gd(gd);
        }
#endif
	m = NULL;

	/*
	 * Cpu twist - cpu localization algorithm
	 */
	if (object) {
		pg_color = gd->gd_cpuid + (pindex & ~ncpus_fit_mask) +
			   (object->pg_color & ~ncpus_fit_mask);
	} else {
		pg_color = gd->gd_cpuid + (pindex & ~ncpus_fit_mask);
	}
	KKASSERT(page_req & 
		(VM_ALLOC_NORMAL|VM_ALLOC_QUICK|
		 VM_ALLOC_INTERRUPT|VM_ALLOC_SYSTEM));

	/*
	 * Certain system threads (pageout daemon, buf_daemon's) are
	 * allowed to eat deeper into the free page list.
	 */
	if (curthread->td_flags & TDF_SYSTHREAD)
		page_req |= VM_ALLOC_SYSTEM;

loop:
	if (vmstats.v_free_count > vmstats.v_free_reserved ||
	    ((page_req & VM_ALLOC_INTERRUPT) && vmstats.v_free_count > 0) ||
	    ((page_req & VM_ALLOC_SYSTEM) && vmstats.v_cache_count == 0 &&
		vmstats.v_free_count > vmstats.v_interrupt_free_min)
	) {
		/*
		 * The free queue has sufficient free pages to take one out.
		 */
		if (page_req & (VM_ALLOC_ZERO | VM_ALLOC_FORCE_ZERO))
			m = vm_page_select_free(pg_color, TRUE);
		else
			m = vm_page_select_free(pg_color, FALSE);
	} else if (page_req & VM_ALLOC_NORMAL) {
		/*
		 * Allocatable from the cache (non-interrupt only).  On
		 * success, we must free the page and try again, thus
		 * ensuring that vmstats.v_*_free_min counters are replenished.
		 */
#ifdef INVARIANTS
		if (curthread->td_preempted) {
			kprintf("vm_page_alloc(): warning, attempt to allocate"
				" cache page from preempting interrupt\n");
			m = NULL;
		} else {
			m = vm_page_select_cache(pg_color);
		}
#else
		m = vm_page_select_cache(pg_color);
#endif
		/*
		 * On success move the page into the free queue and loop.
		 *
		 * Only do this if we can safely acquire the vm_object lock,
		 * because this is effectively a random page and the caller
		 * might be holding the lock shared, we don't want to
		 * deadlock.
		 */
		if (m != NULL) {
			KASSERT(m->dirty == 0,
				("Found dirty cache page %p", m));
			if ((obj = m->object) != NULL) {
				if (vm_object_hold_try(obj)) {
					vm_page_protect(m, VM_PROT_NONE);
					vm_page_free(m);
					/* m->object NULL here */
					vm_object_drop(obj);
				} else {
					vm_page_deactivate(m);
					vm_page_wakeup(m);
				}
			} else {
				vm_page_protect(m, VM_PROT_NONE);
				vm_page_free(m);
			}
			goto loop;
		}

		/*
		 * On failure return NULL
		 */
#if defined(DIAGNOSTIC)
		if (vmstats.v_cache_count > 0)
			kprintf("vm_page_alloc(NORMAL): missing pages on cache queue: %d\n", vmstats.v_cache_count);
#endif
		vm_pageout_deficit++;
		pagedaemon_wakeup();
		return (NULL);
	} else {
		/*
		 * No pages available, wakeup the pageout daemon and give up.
		 */
		vm_pageout_deficit++;
		pagedaemon_wakeup();
		return (NULL);
	}

	/*
	 * v_free_count can race so loop if we don't find the expected
	 * page.
	 */
	if (m == NULL)
		goto loop;

	/*
	 * Good page found.  The page has already been busied for us and
	 * removed from its queues.
	 */
	KASSERT(m->dirty == 0,
		("vm_page_alloc: free/cache page %p was dirty", m));
	KKASSERT(m->queue == PQ_NONE);

#if 0
done:
#endif
	/*
	 * Initialize the structure, inheriting some flags but clearing
	 * all the rest.  The page has already been busied for us.
	 */
	vm_page_flag_clear(m, ~(PG_ZERO | PG_BUSY | PG_SBUSY));
	KKASSERT(m->wire_count == 0);
	KKASSERT(m->busy == 0);
	m->act_count = 0;
	m->valid = 0;

	/*
	 * Caller must be holding the object lock (asserted by
	 * vm_page_insert()).
	 *
	 * NOTE: Inserting a page here does not insert it into any pmaps
	 *	 (which could cause us to block allocating memory).
	 *
	 * NOTE: If no object an unassociated page is allocated, m->pindex
	 *	 can be used by the caller for any purpose.
	 */
	if (object) {
		if (vm_page_insert(m, object, pindex) == FALSE) {
			vm_page_free(m);
			if ((page_req & VM_ALLOC_NULL_OK) == 0)
				panic("PAGE RACE %p[%ld]/%p",
				      object, (long)pindex, m);
			m = NULL;
		}
	} else {
		m->pindex = pindex;
	}

	/*
	 * Don't wakeup too often - wakeup the pageout daemon when
	 * we would be nearly out of memory.
	 */
	pagedaemon_wakeup();

	/*
	 * A PG_BUSY page is returned.
	 */
	return (m);
}

/*
 * Returns number of pages available in our DMA memory reserve
 * (adjusted with vm.dma_reserved=<value>m in /boot/loader.conf)
 */
vm_size_t
vm_contig_avail_pages(void)
{
	alist_blk_t blk;
	alist_blk_t count;
	alist_blk_t bfree;
	spin_lock(&vm_contig_spin);
	bfree = alist_free_info(&vm_contig_alist, &blk, &count);
	spin_unlock(&vm_contig_spin);

	return bfree;
}

/*
 * Attempt to allocate contiguous physical memory with the specified
 * requirements.
 */
vm_page_t
vm_page_alloc_contig(vm_paddr_t low, vm_paddr_t high,
		     unsigned long alignment, unsigned long boundary,
		     unsigned long size, vm_memattr_t memattr)
{
	alist_blk_t blk;
	vm_page_t m;
	int i;

	alignment >>= PAGE_SHIFT;
	if (alignment == 0)
		alignment = 1;
	boundary >>= PAGE_SHIFT;
	if (boundary == 0)
		boundary = 1;
	size = (size + PAGE_MASK) >> PAGE_SHIFT;

	spin_lock(&vm_contig_spin);
	blk = alist_alloc(&vm_contig_alist, 0, size);
	if (blk == ALIST_BLOCK_NONE) {
		spin_unlock(&vm_contig_spin);
		if (bootverbose) {
			kprintf("vm_page_alloc_contig: %ldk nospace\n",
				(size + PAGE_MASK) * (PAGE_SIZE / 1024));
		}
		return(NULL);
	}
	if (high && ((vm_paddr_t)(blk + size) << PAGE_SHIFT) > high) {
		alist_free(&vm_contig_alist, blk, size);
		spin_unlock(&vm_contig_spin);
		if (bootverbose) {
			kprintf("vm_page_alloc_contig: %ldk high "
				"%016jx failed\n",
				(size + PAGE_MASK) * (PAGE_SIZE / 1024),
				(intmax_t)high);
		}
		return(NULL);
	}
	spin_unlock(&vm_contig_spin);
	if (vm_contig_verbose) {
		kprintf("vm_page_alloc_contig: %016jx/%ldk\n",
			(intmax_t)(vm_paddr_t)blk << PAGE_SHIFT,
			(size + PAGE_MASK) * (PAGE_SIZE / 1024));
	}

	m = PHYS_TO_VM_PAGE((vm_paddr_t)blk << PAGE_SHIFT);
	if (memattr != VM_MEMATTR_DEFAULT)
		for (i = 0;i < size;i++)
			pmap_page_set_memattr(&m[i], memattr);
	return m;
}

/*
 * Free contiguously allocated pages.  The pages will be wired but not busy.
 * When freeing to the alist we leave them wired and not busy.
 */
void
vm_page_free_contig(vm_page_t m, unsigned long size)
{
	vm_paddr_t pa = VM_PAGE_TO_PHYS(m);
	vm_pindex_t start = pa >> PAGE_SHIFT;
	vm_pindex_t pages = (size + PAGE_MASK) >> PAGE_SHIFT;

	if (vm_contig_verbose) {
		kprintf("vm_page_free_contig:  %016jx/%ldk\n",
			(intmax_t)pa, size / 1024);
	}
	if (pa < vm_low_phys_reserved) {
		KKASSERT(pa + size <= vm_low_phys_reserved);
		spin_lock(&vm_contig_spin);
		alist_free(&vm_contig_alist, start, pages);
		spin_unlock(&vm_contig_spin);
	} else {
		while (pages) {
			vm_page_busy_wait(m, FALSE, "cpgfr");
			vm_page_unwire(m, 0);
			vm_page_free(m);
			--pages;
			++m;
		}

	}
}


/*
 * Wait for sufficient free memory for nominal heavy memory use kernel
 * operations.
 *
 * WARNING!  Be sure never to call this in any vm_pageout code path, which
 *	     will trivially deadlock the system.
 */
void
vm_wait_nominal(void)
{
	while (vm_page_count_min(0))
		vm_wait(0);
}

/*
 * Test if vm_wait_nominal() would block.
 */
int
vm_test_nominal(void)
{
	if (vm_page_count_min(0))
		return(1);
	return(0);
}

/*
 * Block until free pages are available for allocation, called in various
 * places before memory allocations.
 *
 * The caller may loop if vm_page_count_min() == FALSE so we cannot be
 * more generous then that.
 */
void
vm_wait(int timo)
{
	/*
	 * never wait forever
	 */
	if (timo == 0)
		timo = hz;
	lwkt_gettoken(&vm_token);

	if (curthread == pagethread) {
		/*
		 * The pageout daemon itself needs pages, this is bad.
		 */
		if (vm_page_count_min(0)) {
			vm_pageout_pages_needed = 1;
			tsleep(&vm_pageout_pages_needed, 0, "VMWait", timo);
		}
	} else {
		/*
		 * Wakeup the pageout daemon if necessary and wait.
		 *
		 * Do not wait indefinitely for the target to be reached,
		 * as load might prevent it from being reached any time soon.
		 * But wait a little to try to slow down page allocations
		 * and to give more important threads (the pagedaemon)
		 * allocation priority.
		 */
		if (vm_page_count_target()) {
			if (vm_pages_needed == 0) {
				vm_pages_needed = 1;
				wakeup(&vm_pages_needed);
			}
			++vm_pages_waiting;	/* SMP race ok */
			tsleep(&vmstats.v_free_count, 0, "vmwait", timo);
		}
	}
	lwkt_reltoken(&vm_token);
}

/*
 * Block until free pages are available for allocation
 *
 * Called only from vm_fault so that processes page faulting can be
 * easily tracked.
 */
void
vm_wait_pfault(void)
{
	/*
	 * Wakeup the pageout daemon if necessary and wait.
	 *
	 * Do not wait indefinitely for the target to be reached,
	 * as load might prevent it from being reached any time soon.
	 * But wait a little to try to slow down page allocations
	 * and to give more important threads (the pagedaemon)
	 * allocation priority.
	 */
	if (vm_page_count_min(0)) {
		lwkt_gettoken(&vm_token);
		while (vm_page_count_severe()) {
			if (vm_page_count_target()) {
				if (vm_pages_needed == 0) {
					vm_pages_needed = 1;
					wakeup(&vm_pages_needed);
				}
				++vm_pages_waiting;	/* SMP race ok */
				tsleep(&vmstats.v_free_count, 0, "pfault", hz);
			}
		}
		lwkt_reltoken(&vm_token);
	}
}

/*
 * Put the specified page on the active list (if appropriate).  Ensure
 * that act_count is at least ACT_INIT but do not otherwise mess with it.
 *
 * The caller should be holding the page busied ? XXX
 * This routine may not block.
 */
void
vm_page_activate(vm_page_t m)
{
	u_short oqueue;

	vm_page_spin_lock(m);
	if (m->queue - m->pc != PQ_ACTIVE) {
		_vm_page_queue_spin_lock(m);
		oqueue = _vm_page_rem_queue_spinlocked(m);
		/* page is left spinlocked, queue is unlocked */

		if (oqueue == PQ_CACHE)
			mycpu->gd_cnt.v_reactivated++;
		if (m->wire_count == 0 && (m->flags & PG_UNMANAGED) == 0) {
			if (m->act_count < ACT_INIT)
				m->act_count = ACT_INIT;
			_vm_page_add_queue_spinlocked(m, PQ_ACTIVE + m->pc, 0);
		}
		_vm_page_and_queue_spin_unlock(m);
		if (oqueue == PQ_CACHE || oqueue == PQ_FREE)
			pagedaemon_wakeup();
	} else {
		if (m->act_count < ACT_INIT)
			m->act_count = ACT_INIT;
		vm_page_spin_unlock(m);
	}
}

/*
 * Helper routine for vm_page_free_toq() and vm_page_cache().  This
 * routine is called when a page has been added to the cache or free
 * queues.
 *
 * This routine may not block.
 */
static __inline void
vm_page_free_wakeup(void)
{
	/*
	 * If the pageout daemon itself needs pages, then tell it that
	 * there are some free.
	 */
	if (vm_pageout_pages_needed &&
	    vmstats.v_cache_count + vmstats.v_free_count >= 
	    vmstats.v_pageout_free_min
	) {
		vm_pageout_pages_needed = 0;
		wakeup(&vm_pageout_pages_needed);
	}

	/*
	 * Wakeup processes that are waiting on memory.
	 *
	 * Generally speaking we want to wakeup stuck processes as soon as
	 * possible.  !vm_page_count_min(0) is the absolute minimum point
	 * where we can do this.  Wait a bit longer to reduce degenerate
	 * re-blocking (vm_page_free_hysteresis).  The target check is just
	 * to make sure the min-check w/hysteresis does not exceed the
	 * normal target.
	 */
	if (vm_pages_waiting) {
		if (!vm_page_count_min(vm_page_free_hysteresis) ||
		    !vm_page_count_target()) {
			vm_pages_waiting = 0;
			wakeup(&vmstats.v_free_count);
			++mycpu->gd_cnt.v_ppwakeups;
		}
#if 0
		if (!vm_page_count_target()) {
			/*
			 * Plenty of pages are free, wakeup everyone.
			 */
			vm_pages_waiting = 0;
			wakeup(&vmstats.v_free_count);
			++mycpu->gd_cnt.v_ppwakeups;
		} else if (!vm_page_count_min(0)) {
			/*
			 * Some pages are free, wakeup someone.
			 */
			int wcount = vm_pages_waiting;
			if (wcount > 0)
				--wcount;
			vm_pages_waiting = wcount;
			wakeup_one(&vmstats.v_free_count);
			++mycpu->gd_cnt.v_ppwakeups;
		}
#endif
	}
}

/*
 * Returns the given page to the PQ_FREE or PQ_HOLD list and disassociates
 * it from its VM object.
 *
 * The vm_page must be PG_BUSY on entry.  PG_BUSY will be released on
 * return (the page will have been freed).
 */
void
vm_page_free_toq(vm_page_t m)
{
	mycpu->gd_cnt.v_tfree++;
	KKASSERT((m->flags & PG_MAPPED) == 0);
	KKASSERT(m->flags & PG_BUSY);

	if (m->busy || ((m->queue - m->pc) == PQ_FREE)) {
		kprintf("vm_page_free: pindex(%lu), busy(%d), "
			"PG_BUSY(%d), hold(%d)\n",
			(u_long)m->pindex, m->busy,
			((m->flags & PG_BUSY) ? 1 : 0), m->hold_count);
		if ((m->queue - m->pc) == PQ_FREE)
			panic("vm_page_free: freeing free page");
		else
			panic("vm_page_free: freeing busy page");
	}

	/*
	 * Remove from object, spinlock the page and its queues and
	 * remove from any queue.  No queue spinlock will be held
	 * after this section (because the page was removed from any
	 * queue).
	 */
	vm_page_remove(m);
	vm_page_and_queue_spin_lock(m);
	_vm_page_rem_queue_spinlocked(m);

	/*
	 * No further management of fictitious pages occurs beyond object
	 * and queue removal.
	 */
	if ((m->flags & PG_FICTITIOUS) != 0) {
		vm_page_spin_unlock(m);
		vm_page_wakeup(m);
		return;
	}

	m->valid = 0;
	vm_page_undirty(m);

	if (m->wire_count != 0) {
		if (m->wire_count > 1) {
		    panic(
			"vm_page_free: invalid wire count (%d), pindex: 0x%lx",
			m->wire_count, (long)m->pindex);
		}
		panic("vm_page_free: freeing wired page");
	}

	/*
	 * Clear the UNMANAGED flag when freeing an unmanaged page.
	 * Clear the NEED_COMMIT flag
	 */
	if (m->flags & PG_UNMANAGED)
		vm_page_flag_clear(m, PG_UNMANAGED);
	if (m->flags & PG_NEED_COMMIT)
		vm_page_flag_clear(m, PG_NEED_COMMIT);

	if (m->hold_count != 0) {
		vm_page_flag_clear(m, PG_ZERO);
		_vm_page_add_queue_spinlocked(m, PQ_HOLD + m->pc, 0);
	} else {
		_vm_page_add_queue_spinlocked(m, PQ_FREE + m->pc, 0);
	}

	/*
	 * This sequence allows us to clear PG_BUSY while still holding
	 * its spin lock, which reduces contention vs allocators.  We
	 * must not leave the queue locked or _vm_page_wakeup() may
	 * deadlock.
	 */
	_vm_page_queue_spin_unlock(m);
	if (_vm_page_wakeup(m)) {
		vm_page_spin_unlock(m);
		wakeup(m);
	} else {
		vm_page_spin_unlock(m);
	}
	vm_page_free_wakeup();
}

/*
 * vm_page_free_fromq_fast()
 *
 * Remove a non-zero page from one of the free queues; the page is removed for
 * zeroing, so do not issue a wakeup.
 */
vm_page_t
vm_page_free_fromq_fast(void)
{
	static int qi;
	vm_page_t m;
	int i;

	for (i = 0; i < PQ_L2_SIZE; ++i) {
		m = vm_page_list_find(PQ_FREE, qi, FALSE);
		/* page is returned spinlocked and removed from its queue */
		if (m) {
			if (vm_page_busy_try(m, TRUE)) {
				/*
				 * We were unable to busy the page, deactivate
				 * it and loop.
				 */
				_vm_page_deactivate_locked(m, 0);
				vm_page_spin_unlock(m);
			} else if (m->flags & PG_ZERO) {
				/*
				 * The page is already PG_ZERO, requeue it and loop
				 */
				_vm_page_add_queue_spinlocked(m,
							      PQ_FREE + m->pc,
							      0);
				vm_page_queue_spin_unlock(m);
				if (_vm_page_wakeup(m)) {
					vm_page_spin_unlock(m);
					wakeup(m);
				} else {
					vm_page_spin_unlock(m);
				}
			} else {
				/*
				 * The page is not PG_ZERO'd so return it.
				 */
				KKASSERT((m->flags & (PG_UNMANAGED |
						      PG_NEED_COMMIT)) == 0);
				KKASSERT(m->hold_count == 0);
				KKASSERT(m->wire_count == 0);
				vm_page_spin_unlock(m);
				break;
			}
			m = NULL;
		}
		qi = (qi + PQ_PRIME2) & PQ_L2_MASK;
	}
	return (m);
}

/*
 * vm_page_unmanage()
 *
 * Prevent PV management from being done on the page.  The page is
 * removed from the paging queues as if it were wired, and as a 
 * consequence of no longer being managed the pageout daemon will not
 * touch it (since there is no way to locate the pte mappings for the
 * page).  madvise() calls that mess with the pmap will also no longer
 * operate on the page.
 *
 * Beyond that the page is still reasonably 'normal'.  Freeing the page
 * will clear the flag.
 *
 * This routine is used by OBJT_PHYS objects - objects using unswappable
 * physical memory as backing store rather then swap-backed memory and
 * will eventually be extended to support 4MB unmanaged physical 
 * mappings.
 *
 * Caller must be holding the page busy.
 */
void
vm_page_unmanage(vm_page_t m)
{
	KKASSERT(m->flags & PG_BUSY);
	if ((m->flags & PG_UNMANAGED) == 0) {
		if (m->wire_count == 0)
			vm_page_unqueue(m);
	}
	vm_page_flag_set(m, PG_UNMANAGED);
}

/*
 * Mark this page as wired down by yet another map, removing it from
 * paging queues as necessary.
 *
 * Caller must be holding the page busy.
 */
void
vm_page_wire(vm_page_t m)
{
	/*
	 * Only bump the wire statistics if the page is not already wired,
	 * and only unqueue the page if it is on some queue (if it is unmanaged
	 * it is already off the queues).  Don't do anything with fictitious
	 * pages because they are always wired.
	 */
	KKASSERT(m->flags & PG_BUSY);
	if ((m->flags & PG_FICTITIOUS) == 0) {
		if (atomic_fetchadd_int(&m->wire_count, 1) == 0) {
			if ((m->flags & PG_UNMANAGED) == 0)
				vm_page_unqueue(m);
			atomic_add_int(&vmstats.v_wire_count, 1);
		}
		KASSERT(m->wire_count != 0,
			("vm_page_wire: wire_count overflow m=%p", m));
	}
}

/*
 * Release one wiring of this page, potentially enabling it to be paged again.
 *
 * Many pages placed on the inactive queue should actually go
 * into the cache, but it is difficult to figure out which.  What
 * we do instead, if the inactive target is well met, is to put
 * clean pages at the head of the inactive queue instead of the tail.
 * This will cause them to be moved to the cache more quickly and
 * if not actively re-referenced, freed more quickly.  If we just
 * stick these pages at the end of the inactive queue, heavy filesystem
 * meta-data accesses can cause an unnecessary paging load on memory bound 
 * processes.  This optimization causes one-time-use metadata to be
 * reused more quickly.
 *
 * Pages marked PG_NEED_COMMIT are always activated and never placed on
 * the inactive queue.  This helps the pageout daemon determine memory
 * pressure and act on out-of-memory situations more quickly.
 *
 * BUT, if we are in a low-memory situation we have no choice but to
 * put clean pages on the cache queue.
 *
 * A number of routines use vm_page_unwire() to guarantee that the page
 * will go into either the inactive or active queues, and will NEVER
 * be placed in the cache - for example, just after dirtying a page.
 * dirty pages in the cache are not allowed.
 *
 * This routine may not block.
 */
void
vm_page_unwire(vm_page_t m, int activate)
{
	KKASSERT(m->flags & PG_BUSY);
	if (m->flags & PG_FICTITIOUS) {
		/* do nothing */
	} else if (m->wire_count <= 0) {
		panic("vm_page_unwire: invalid wire count: %d", m->wire_count);
	} else {
		if (atomic_fetchadd_int(&m->wire_count, -1) == 1) {
			atomic_add_int(&vmstats.v_wire_count, -1);
			if (m->flags & PG_UNMANAGED) {
				;
			} else if (activate || (m->flags & PG_NEED_COMMIT)) {
				vm_page_spin_lock(m);
				_vm_page_add_queue_spinlocked(m,
							PQ_ACTIVE + m->pc, 0);
				_vm_page_and_queue_spin_unlock(m);
			} else {
				vm_page_spin_lock(m);
				vm_page_flag_clear(m, PG_WINATCFLS);
				_vm_page_add_queue_spinlocked(m,
							PQ_INACTIVE + m->pc, 0);
				++vm_swapcache_inactive_heuristic;
				_vm_page_and_queue_spin_unlock(m);
			}
		}
	}
}

/*
 * Move the specified page to the inactive queue.  If the page has
 * any associated swap, the swap is deallocated.
 *
 * Normally athead is 0 resulting in LRU operation.  athead is set
 * to 1 if we want this page to be 'as if it were placed in the cache',
 * except without unmapping it from the process address space.
 *
 * vm_page's spinlock must be held on entry and will remain held on return.
 * This routine may not block.
 */
static void
_vm_page_deactivate_locked(vm_page_t m, int athead)
{
	u_short oqueue;

	/*
	 * Ignore if already inactive.
	 */
	if (m->queue - m->pc == PQ_INACTIVE)
		return;
	_vm_page_queue_spin_lock(m);
	oqueue = _vm_page_rem_queue_spinlocked(m);

	if (m->wire_count == 0 && (m->flags & PG_UNMANAGED) == 0) {
		if (oqueue == PQ_CACHE)
			mycpu->gd_cnt.v_reactivated++;
		vm_page_flag_clear(m, PG_WINATCFLS);
		_vm_page_add_queue_spinlocked(m, PQ_INACTIVE + m->pc, athead);
		if (athead == 0)
			++vm_swapcache_inactive_heuristic;
	}
	/* NOTE: PQ_NONE if condition not taken */
	_vm_page_queue_spin_unlock(m);
	/* leaves vm_page spinlocked */
}

/*
 * Attempt to deactivate a page.
 *
 * No requirements.
 */
void
vm_page_deactivate(vm_page_t m)
{
	vm_page_spin_lock(m);
	_vm_page_deactivate_locked(m, 0);
	vm_page_spin_unlock(m);
}

void
vm_page_deactivate_locked(vm_page_t m)
{
	_vm_page_deactivate_locked(m, 0);
}

/*
 * Attempt to move a page to PQ_CACHE.
 *
 * Returns 0 on failure, 1 on success
 *
 * The page should NOT be busied by the caller.  This function will validate
 * whether the page can be safely moved to the cache.
 */
int
vm_page_try_to_cache(vm_page_t m)
{
	vm_page_spin_lock(m);
	if (vm_page_busy_try(m, TRUE)) {
		vm_page_spin_unlock(m);
		return(0);
	}
	if (m->dirty || m->hold_count || m->wire_count ||
	    (m->flags & (PG_UNMANAGED | PG_NEED_COMMIT))) {
		if (_vm_page_wakeup(m)) {
			vm_page_spin_unlock(m);
			wakeup(m);
		} else {
			vm_page_spin_unlock(m);
		}
		return(0);
	}
	vm_page_spin_unlock(m);

	/*
	 * Page busied by us and no longer spinlocked.  Dirty pages cannot
	 * be moved to the cache.
	 */
	vm_page_test_dirty(m);
	if (m->dirty || (m->flags & PG_NEED_COMMIT)) {
		vm_page_wakeup(m);
		return(0);
	}
	vm_page_cache(m);
	return(1);
}

/*
 * Attempt to free the page.  If we cannot free it, we do nothing.
 * 1 is returned on success, 0 on failure.
 *
 * No requirements.
 */
int
vm_page_try_to_free(vm_page_t m)
{
	vm_page_spin_lock(m);
	if (vm_page_busy_try(m, TRUE)) {
		vm_page_spin_unlock(m);
		return(0);
	}

	/*
	 * The page can be in any state, including already being on the free
	 * queue.  Check to see if it really can be freed.
	 */
	if (m->dirty ||				/* can't free if it is dirty */
	    m->hold_count ||			/* or held (XXX may be wrong) */
	    m->wire_count ||			/* or wired */
	    (m->flags & (PG_UNMANAGED |		/* or unmanaged */
			 PG_NEED_COMMIT)) ||	/* or needs a commit */
	    m->queue - m->pc == PQ_FREE ||	/* already on PQ_FREE */
	    m->queue - m->pc == PQ_HOLD) {	/* already on PQ_HOLD */
		if (_vm_page_wakeup(m)) {
			vm_page_spin_unlock(m);
			wakeup(m);
		} else {
			vm_page_spin_unlock(m);
		}
		return(0);
	}
	vm_page_spin_unlock(m);

	/*
	 * We can probably free the page.
	 *
	 * Page busied by us and no longer spinlocked.  Dirty pages will
	 * not be freed by this function.    We have to re-test the
	 * dirty bit after cleaning out the pmaps.
	 */
	vm_page_test_dirty(m);
	if (m->dirty || (m->flags & PG_NEED_COMMIT)) {
		vm_page_wakeup(m);
		return(0);
	}
	vm_page_protect(m, VM_PROT_NONE);
	if (m->dirty || (m->flags & PG_NEED_COMMIT)) {
		vm_page_wakeup(m);
		return(0);
	}
	vm_page_free(m);
	return(1);
}

/*
 * vm_page_cache
 *
 * Put the specified page onto the page cache queue (if appropriate).
 *
 * The page must be busy, and this routine will release the busy and
 * possibly even free the page.
 */
void
vm_page_cache(vm_page_t m)
{
	if ((m->flags & (PG_UNMANAGED | PG_NEED_COMMIT)) ||
	    m->busy || m->wire_count || m->hold_count) {
		kprintf("vm_page_cache: attempting to cache busy/held page\n");
		vm_page_wakeup(m);
		return;
	}

	/*
	 * Already in the cache (and thus not mapped)
	 */
	if ((m->queue - m->pc) == PQ_CACHE) {
		KKASSERT((m->flags & PG_MAPPED) == 0);
		vm_page_wakeup(m);
		return;
	}

	/*
	 * Caller is required to test m->dirty, but note that the act of
	 * removing the page from its maps can cause it to become dirty
	 * on an SMP system due to another cpu running in usermode.
	 */
	if (m->dirty) {
		panic("vm_page_cache: caching a dirty page, pindex: %ld",
			(long)m->pindex);
	}

	/*
	 * Remove all pmaps and indicate that the page is not
	 * writeable or mapped.  Our vm_page_protect() call may
	 * have blocked (especially w/ VM_PROT_NONE), so recheck
	 * everything.
	 */
	vm_page_protect(m, VM_PROT_NONE);
	if ((m->flags & (PG_UNMANAGED | PG_MAPPED)) ||
	    m->busy || m->wire_count || m->hold_count) {
		vm_page_wakeup(m);
	} else if (m->dirty || (m->flags & PG_NEED_COMMIT)) {
		vm_page_deactivate(m);
		vm_page_wakeup(m);
	} else {
		_vm_page_and_queue_spin_lock(m);
		_vm_page_rem_queue_spinlocked(m);
		_vm_page_add_queue_spinlocked(m, PQ_CACHE + m->pc, 0);
		_vm_page_queue_spin_unlock(m);
		if (_vm_page_wakeup(m)) {
			vm_page_spin_unlock(m);
			wakeup(m);
		} else {
			vm_page_spin_unlock(m);
		}
		vm_page_free_wakeup();
	}
}

/*
 * vm_page_dontneed()
 *
 * Cache, deactivate, or do nothing as appropriate.  This routine
 * is typically used by madvise() MADV_DONTNEED.
 *
 * Generally speaking we want to move the page into the cache so
 * it gets reused quickly.  However, this can result in a silly syndrome
 * due to the page recycling too quickly.  Small objects will not be
 * fully cached.  On the otherhand, if we move the page to the inactive
 * queue we wind up with a problem whereby very large objects 
 * unnecessarily blow away our inactive and cache queues.
 *
 * The solution is to move the pages based on a fixed weighting.  We
 * either leave them alone, deactivate them, or move them to the cache,
 * where moving them to the cache has the highest weighting.
 * By forcing some pages into other queues we eventually force the
 * system to balance the queues, potentially recovering other unrelated
 * space from active.  The idea is to not force this to happen too
 * often.
 *
 * The page must be busied.
 */
void
vm_page_dontneed(vm_page_t m)
{
	static int dnweight;
	int dnw;
	int head;

	dnw = ++dnweight;

	/*
	 * occassionally leave the page alone
	 */
	if ((dnw & 0x01F0) == 0 ||
	    m->queue - m->pc == PQ_INACTIVE ||
	    m->queue - m->pc == PQ_CACHE
	) {
		if (m->act_count >= ACT_INIT)
			--m->act_count;
		return;
	}

	/*
	 * If vm_page_dontneed() is inactivating a page, it must clear
	 * the referenced flag; otherwise the pagedaemon will see references
	 * on the page in the inactive queue and reactivate it. Until the 
	 * page can move to the cache queue, madvise's job is not done.
	 */
	vm_page_flag_clear(m, PG_REFERENCED);
	pmap_clear_reference(m);

	if (m->dirty == 0)
		vm_page_test_dirty(m);

	if (m->dirty || (dnw & 0x0070) == 0) {
		/*
		 * Deactivate the page 3 times out of 32.
		 */
		head = 0;
	} else {
		/*
		 * Cache the page 28 times out of every 32.  Note that
		 * the page is deactivated instead of cached, but placed
		 * at the head of the queue instead of the tail.
		 */
		head = 1;
	}
	vm_page_spin_lock(m);
	_vm_page_deactivate_locked(m, head);
	vm_page_spin_unlock(m);
}

/*
 * These routines manipulate the 'soft busy' count for a page.  A soft busy
 * is almost like PG_BUSY except that it allows certain compatible operations
 * to occur on the page while it is busy.  For example, a page undergoing a
 * write can still be mapped read-only.
 *
 * Because vm_pages can overlap buffers m->busy can be > 1.  m->busy is only
 * adjusted while the vm_page is PG_BUSY so the flash will occur when the
 * busy bit is cleared.
 */
void
vm_page_io_start(vm_page_t m)
{
        KASSERT(m->flags & PG_BUSY, ("vm_page_io_start: page not busy!!!"));
        atomic_add_char(&m->busy, 1);
	vm_page_flag_set(m, PG_SBUSY);
}

void
vm_page_io_finish(vm_page_t m)
{
        KASSERT(m->flags & PG_BUSY, ("vm_page_io_finish: page not busy!!!"));
        atomic_subtract_char(&m->busy, 1);
	if (m->busy == 0)
		vm_page_flag_clear(m, PG_SBUSY);
}

/*
 * Indicate that a clean VM page requires a filesystem commit and cannot
 * be reused.  Used by tmpfs.
 */
void
vm_page_need_commit(vm_page_t m)
{
	vm_page_flag_set(m, PG_NEED_COMMIT);
	vm_object_set_writeable_dirty(m->object);
}

void
vm_page_clear_commit(vm_page_t m)
{
	vm_page_flag_clear(m, PG_NEED_COMMIT);
}

/*
 * Grab a page, blocking if it is busy and allocating a page if necessary.
 * A busy page is returned or NULL.  The page may or may not be valid and
 * might not be on a queue (the caller is responsible for the disposition of
 * the page).
 *
 * If VM_ALLOC_ZERO is specified and the grab must allocate a new page, the
 * page will be zero'd and marked valid.
 *
 * If VM_ALLOC_FORCE_ZERO is specified the page will be zero'd and marked
 * valid even if it already exists.
 *
 * If VM_ALLOC_RETRY is specified this routine will never return NULL.  Also
 * note that VM_ALLOC_NORMAL must be specified if VM_ALLOC_RETRY is specified.
 * VM_ALLOC_NULL_OK is implied when VM_ALLOC_RETRY is specified.
 *
 * This routine may block, but if VM_ALLOC_RETRY is not set then NULL is
 * always returned if we had blocked.  
 *
 * This routine may not be called from an interrupt.
 *
 * PG_ZERO is *ALWAYS* cleared by this routine.
 *
 * No other requirements.
 */
vm_page_t
vm_page_grab(vm_object_t object, vm_pindex_t pindex, int allocflags)
{
	vm_page_t m;
	int error;
	int shared = 1;

	KKASSERT(allocflags &
		(VM_ALLOC_NORMAL|VM_ALLOC_INTERRUPT|VM_ALLOC_SYSTEM));
	vm_object_hold_shared(object);
	for (;;) {
		m = vm_page_lookup_busy_try(object, pindex, TRUE, &error);
		if (error) {
			vm_page_sleep_busy(m, TRUE, "pgrbwt");
			if ((allocflags & VM_ALLOC_RETRY) == 0) {
				m = NULL;
				break;
			}
			/* retry */
		} else if (m == NULL) {
			if (shared) {
				vm_object_upgrade(object);
				shared = 0;
			}
			if (allocflags & VM_ALLOC_RETRY)
				allocflags |= VM_ALLOC_NULL_OK;
			m = vm_page_alloc(object, pindex,
					  allocflags & ~VM_ALLOC_RETRY);
			if (m)
				break;
			vm_wait(0);
			if ((allocflags & VM_ALLOC_RETRY) == 0)
				goto failed;
		} else {
			/* m found */
			break;
		}
	}

	/*
	 * If VM_ALLOC_ZERO an invalid page will be zero'd and set valid.
	 *
	 * If VM_ALLOC_FORCE_ZERO the page is unconditionally zero'd and set
	 * valid even if already valid.
	 */
	if (m->valid == 0) {
		if (allocflags & (VM_ALLOC_ZERO | VM_ALLOC_FORCE_ZERO)) {
			if ((m->flags & PG_ZERO) == 0)
				pmap_zero_page(VM_PAGE_TO_PHYS(m));
			m->valid = VM_PAGE_BITS_ALL;
		}
	} else if (allocflags & VM_ALLOC_FORCE_ZERO) {
		pmap_zero_page(VM_PAGE_TO_PHYS(m));
		m->valid = VM_PAGE_BITS_ALL;
	}
	vm_page_flag_clear(m, PG_ZERO);
failed:
	vm_object_drop(object);
	return(m);
}

/*
 * Mapping function for valid bits or for dirty bits in
 * a page.  May not block.
 *
 * Inputs are required to range within a page.
 *
 * No requirements.
 * Non blocking.
 */
int
vm_page_bits(int base, int size)
{
	int first_bit;
	int last_bit;

	KASSERT(
	    base + size <= PAGE_SIZE,
	    ("vm_page_bits: illegal base/size %d/%d", base, size)
	);

	if (size == 0)		/* handle degenerate case */
		return(0);

	first_bit = base >> DEV_BSHIFT;
	last_bit = (base + size - 1) >> DEV_BSHIFT;

	return ((2 << last_bit) - (1 << first_bit));
}

/*
 * Sets portions of a page valid and clean.  The arguments are expected
 * to be DEV_BSIZE aligned but if they aren't the bitmap is inclusive
 * of any partial chunks touched by the range.  The invalid portion of
 * such chunks will be zero'd.
 *
 * NOTE: When truncating a buffer vnode_pager_setsize() will automatically
 *	 align base to DEV_BSIZE so as not to mark clean a partially
 *	 truncated device block.  Otherwise the dirty page status might be
 *	 lost.
 *
 * This routine may not block.
 *
 * (base + size) must be less then or equal to PAGE_SIZE.
 */
static void
_vm_page_zero_valid(vm_page_t m, int base, int size)
{
	int frag;
	int endoff;

	if (size == 0)	/* handle degenerate case */
		return;

	/*
	 * If the base is not DEV_BSIZE aligned and the valid
	 * bit is clear, we have to zero out a portion of the
	 * first block.
	 */

	if ((frag = base & ~(DEV_BSIZE - 1)) != base &&
	    (m->valid & (1 << (base >> DEV_BSHIFT))) == 0
	) {
		pmap_zero_page_area(
		    VM_PAGE_TO_PHYS(m),
		    frag,
		    base - frag
		);
	}

	/*
	 * If the ending offset is not DEV_BSIZE aligned and the 
	 * valid bit is clear, we have to zero out a portion of
	 * the last block.
	 */

	endoff = base + size;

	if ((frag = endoff & ~(DEV_BSIZE - 1)) != endoff &&
	    (m->valid & (1 << (endoff >> DEV_BSHIFT))) == 0
	) {
		pmap_zero_page_area(
		    VM_PAGE_TO_PHYS(m),
		    endoff,
		    DEV_BSIZE - (endoff & (DEV_BSIZE - 1))
		);
	}
}

/*
 * Set valid, clear dirty bits.  If validating the entire
 * page we can safely clear the pmap modify bit.  We also
 * use this opportunity to clear the PG_NOSYNC flag.  If a process
 * takes a write fault on a MAP_NOSYNC memory area the flag will
 * be set again.
 *
 * We set valid bits inclusive of any overlap, but we can only
 * clear dirty bits for DEV_BSIZE chunks that are fully within
 * the range.
 *
 * Page must be busied?
 * No other requirements.
 */
void
vm_page_set_valid(vm_page_t m, int base, int size)
{
	_vm_page_zero_valid(m, base, size);
	m->valid |= vm_page_bits(base, size);
}


/*
 * Set valid bits and clear dirty bits.
 *
 * NOTE: This function does not clear the pmap modified bit.
 *	 Also note that e.g. NFS may use a byte-granular base
 *	 and size.
 *
 * WARNING: Page must be busied?  But vfs_clean_one_page() will call
 *	    this without necessarily busying the page (via bdwrite()).
 *	    So for now vm_token must also be held.
 *
 * No other requirements.
 */
void
vm_page_set_validclean(vm_page_t m, int base, int size)
{
	int pagebits;

	_vm_page_zero_valid(m, base, size);
	pagebits = vm_page_bits(base, size);
	m->valid |= pagebits;
	m->dirty &= ~pagebits;
	if (base == 0 && size == PAGE_SIZE) {
		/*pmap_clear_modify(m);*/
		vm_page_flag_clear(m, PG_NOSYNC);
	}
}

/*
 * Set valid & dirty.  Used by buwrite()
 *
 * WARNING: Page must be busied?  But vfs_dirty_one_page() will
 *	    call this function in buwrite() so for now vm_token must
 *	    be held.
 *
 * No other requirements.
 */
void
vm_page_set_validdirty(vm_page_t m, int base, int size)
{
	int pagebits;

	pagebits = vm_page_bits(base, size);
	m->valid |= pagebits;
	m->dirty |= pagebits;
	if (m->object)
	       vm_object_set_writeable_dirty(m->object);
}

/*
 * Clear dirty bits.
 *
 * NOTE: This function does not clear the pmap modified bit.
 *	 Also note that e.g. NFS may use a byte-granular base
 *	 and size.
 *
 * Page must be busied?
 * No other requirements.
 */
void
vm_page_clear_dirty(vm_page_t m, int base, int size)
{
	m->dirty &= ~vm_page_bits(base, size);
	if (base == 0 && size == PAGE_SIZE) {
		/*pmap_clear_modify(m);*/
		vm_page_flag_clear(m, PG_NOSYNC);
	}
}

/*
 * Make the page all-dirty.
 *
 * Also make sure the related object and vnode reflect the fact that the
 * object may now contain a dirty page.
 *
 * Page must be busied?
 * No other requirements.
 */
void
vm_page_dirty(vm_page_t m)
{
#ifdef INVARIANTS
        int pqtype = m->queue - m->pc;
#endif
        KASSERT(pqtype != PQ_CACHE && pqtype != PQ_FREE,
                ("vm_page_dirty: page in free/cache queue!"));
	if (m->dirty != VM_PAGE_BITS_ALL) {
		m->dirty = VM_PAGE_BITS_ALL;
		if (m->object)
			vm_object_set_writeable_dirty(m->object);
	}
}

/*
 * Invalidates DEV_BSIZE'd chunks within a page.  Both the
 * valid and dirty bits for the effected areas are cleared.
 *
 * Page must be busied?
 * Does not block.
 * No other requirements.
 */
void
vm_page_set_invalid(vm_page_t m, int base, int size)
{
	int bits;

	bits = vm_page_bits(base, size);
	m->valid &= ~bits;
	m->dirty &= ~bits;
	m->object->generation++;
}

/*
 * The kernel assumes that the invalid portions of a page contain 
 * garbage, but such pages can be mapped into memory by user code.
 * When this occurs, we must zero out the non-valid portions of the
 * page so user code sees what it expects.
 *
 * Pages are most often semi-valid when the end of a file is mapped 
 * into memory and the file's size is not page aligned.
 *
 * Page must be busied?
 * No other requirements.
 */
void
vm_page_zero_invalid(vm_page_t m, boolean_t setvalid)
{
	int b;
	int i;

	/*
	 * Scan the valid bits looking for invalid sections that
	 * must be zerod.  Invalid sub-DEV_BSIZE'd areas ( where the
	 * valid bit may be set ) have already been zerod by
	 * vm_page_set_validclean().
	 */
	for (b = i = 0; i <= PAGE_SIZE / DEV_BSIZE; ++i) {
		if (i == (PAGE_SIZE / DEV_BSIZE) || 
		    (m->valid & (1 << i))
		) {
			if (i > b) {
				pmap_zero_page_area(
				    VM_PAGE_TO_PHYS(m), 
				    b << DEV_BSHIFT,
				    (i - b) << DEV_BSHIFT
				);
			}
			b = i + 1;
		}
	}

	/*
	 * setvalid is TRUE when we can safely set the zero'd areas
	 * as being valid.  We can do this if there are no cache consistency
	 * issues.  e.g. it is ok to do with UFS, but not ok to do with NFS.
	 */
	if (setvalid)
		m->valid = VM_PAGE_BITS_ALL;
}

/*
 * Is a (partial) page valid?  Note that the case where size == 0
 * will return FALSE in the degenerate case where the page is entirely
 * invalid, and TRUE otherwise.
 *
 * Does not block.
 * No other requirements.
 */
int
vm_page_is_valid(vm_page_t m, int base, int size)
{
	int bits = vm_page_bits(base, size);

	if (m->valid && ((m->valid & bits) == bits))
		return 1;
	else
		return 0;
}

/*
 * update dirty bits from pmap/mmu.  May not block.
 *
 * Caller must hold the page busy
 */
void
vm_page_test_dirty(vm_page_t m)
{
	if ((m->dirty != VM_PAGE_BITS_ALL) && pmap_is_modified(m)) {
		vm_page_dirty(m);
	}
}

/*
 * Register an action, associating it with its vm_page
 */
void
vm_page_register_action(vm_page_action_t action, vm_page_event_t event)
{
	struct vm_page_action_list *list;
	int hv;

	hv = (int)((intptr_t)action->m >> 8) & VMACTION_HMASK;
	list = &action_list[hv];

	lwkt_gettoken(&vm_token);
	vm_page_flag_set(action->m, PG_ACTIONLIST);
	action->event = event;
	LIST_INSERT_HEAD(list, action, entry);
	lwkt_reltoken(&vm_token);
}

/*
 * Unregister an action, disassociating it from its related vm_page
 */
void
vm_page_unregister_action(vm_page_action_t action)
{
	struct vm_page_action_list *list;
	int hv;

	lwkt_gettoken(&vm_token);
	if (action->event != VMEVENT_NONE) {
		action->event = VMEVENT_NONE;
		LIST_REMOVE(action, entry);

		hv = (int)((intptr_t)action->m >> 8) & VMACTION_HMASK;
		list = &action_list[hv];
		if (LIST_EMPTY(list))
			vm_page_flag_clear(action->m, PG_ACTIONLIST);
	}
	lwkt_reltoken(&vm_token);
}

/*
 * Issue an event on a VM page.  Corresponding action structures are
 * removed from the page's list and called.
 *
 * If the vm_page has no more pending action events we clear its
 * PG_ACTIONLIST flag.
 */
void
vm_page_event_internal(vm_page_t m, vm_page_event_t event)
{
	struct vm_page_action_list *list;
	struct vm_page_action *scan;
	struct vm_page_action *next;
	int hv;
	int all;

	hv = (int)((intptr_t)m >> 8) & VMACTION_HMASK;
	list = &action_list[hv];
	all = 1;

	lwkt_gettoken(&vm_token);
	LIST_FOREACH_MUTABLE(scan, list, entry, next) {
		if (scan->m == m) {
			if (scan->event == event) {
				scan->event = VMEVENT_NONE;
				LIST_REMOVE(scan, entry);
				scan->func(m, scan);
				/* XXX */
			} else {
				all = 0;
			}
		}
	}
	if (all)
		vm_page_flag_clear(m, PG_ACTIONLIST);
	lwkt_reltoken(&vm_token);
}

#include "opt_ddb.h"
#ifdef DDB
#include <sys/kernel.h>

#include <ddb/ddb.h>

DB_SHOW_COMMAND(page, vm_page_print_page_info)
{
	db_printf("vmstats.v_free_count: %d\n", vmstats.v_free_count);
	db_printf("vmstats.v_cache_count: %d\n", vmstats.v_cache_count);
	db_printf("vmstats.v_inactive_count: %d\n", vmstats.v_inactive_count);
	db_printf("vmstats.v_active_count: %d\n", vmstats.v_active_count);
	db_printf("vmstats.v_wire_count: %d\n", vmstats.v_wire_count);
	db_printf("vmstats.v_free_reserved: %d\n", vmstats.v_free_reserved);
	db_printf("vmstats.v_free_min: %d\n", vmstats.v_free_min);
	db_printf("vmstats.v_free_target: %d\n", vmstats.v_free_target);
	db_printf("vmstats.v_cache_min: %d\n", vmstats.v_cache_min);
	db_printf("vmstats.v_inactive_target: %d\n", vmstats.v_inactive_target);
}

DB_SHOW_COMMAND(pageq, vm_page_print_pageq_info)
{
	int i;
	db_printf("PQ_FREE:");
	for(i=0;i<PQ_L2_SIZE;i++) {
		db_printf(" %d", vm_page_queues[PQ_FREE + i].lcnt);
	}
	db_printf("\n");
		
	db_printf("PQ_CACHE:");
	for(i=0;i<PQ_L2_SIZE;i++) {
		db_printf(" %d", vm_page_queues[PQ_CACHE + i].lcnt);
	}
	db_printf("\n");

	db_printf("PQ_ACTIVE:");
	for(i=0;i<PQ_L2_SIZE;i++) {
		db_printf(" %d", vm_page_queues[PQ_ACTIVE + i].lcnt);
	}
	db_printf("\n");

	db_printf("PQ_INACTIVE:");
	for(i=0;i<PQ_L2_SIZE;i++) {
		db_printf(" %d", vm_page_queues[PQ_INACTIVE + i].lcnt);
	}
	db_printf("\n");
}
#endif /* DDB */
