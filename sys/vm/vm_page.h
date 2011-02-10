/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	from: @(#)vm_page.h	8.2 (Berkeley) 12/13/93
 *
 *
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
 *
 * $FreeBSD: src/sys/vm/vm_page.h,v 1.75.2.8 2002/03/06 01:07:09 dillon Exp $
 */

/*
 *	Resident memory system definitions.
 */

#ifndef	_VM_VM_PAGE_H_
#define	_VM_VM_PAGE_H_

#if !defined(KLD_MODULE) && defined(_KERNEL)
#include "opt_vmpage.h"
#endif

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _MACHINE_PMAP_H_
#include <machine/pmap.h>
#endif
#ifndef _VM_PMAP_H_
#include <vm/pmap.h>
#endif
#include <machine/atomic.h>

#ifdef _KERNEL

#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>
#endif
#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>
#endif

#ifdef __x86_64__
#include <machine/vmparam.h>
#endif

#endif

typedef enum vm_page_event { VMEVENT_NONE, VMEVENT_COW } vm_page_event_t;

struct vm_page_action {
	LIST_ENTRY(vm_page_action) entry;
	struct vm_page		*m;
	vm_page_event_t		event;
	void			(*func)(struct vm_page *,
					struct vm_page_action *);
	void			*data;
};

typedef struct vm_page_action *vm_page_action_t;

/*
 *	Management of resident (logical) pages.
 *
 *	A small structure is kept for each resident
 *	page, indexed by page number.  Each structure
 *	is an element of several lists:
 *
 *		A hash table bucket used to quickly
 *		perform object/offset lookups
 *
 *		A list of all pages for a given object,
 *		so they can be quickly deactivated at
 *		time of deallocation.
 *
 *		An ordered list of pages due for pageout.
 *
 *	In addition, the structure contains the object
 *	and offset to which this page belongs (for pageout),
 *	and sundry status bits.
 *
 *	Fields in this structure are locked either by the lock on the
 *	object that the page belongs to (O) or by the lock on the page
 *	queues (P).
 *
 *	The 'valid' and 'dirty' fields are distinct.  A page may have dirty
 *	bits set without having associated valid bits set.  This is used by
 *	NFS to implement piecemeal writes.
 */

TAILQ_HEAD(pglist, vm_page);

struct vm_object;

int rb_vm_page_compare(struct vm_page *, struct vm_page *);

struct vm_page_rb_tree;
RB_PROTOTYPE2(vm_page_rb_tree, vm_page, rb_entry, rb_vm_page_compare, vm_pindex_t);

struct vm_page {
	TAILQ_ENTRY(vm_page) pageq;	/* vm_page_queues[] list (P)	*/
	RB_ENTRY(vm_page) rb_entry;	/* Red-Black tree based at object */

	struct vm_object *object;	/* which object am I in (O,P)*/
	vm_pindex_t pindex;		/* offset into object (O,P) */
	vm_paddr_t phys_addr;		/* physical address of page */
	struct md_page md;		/* machine dependant stuff */
	u_short	queue;			/* page queue index */
	u_short	pc;			/* page color */
	u_char	act_count;		/* page usage count */
	u_char	busy;			/* page busy count */
	u_char	unused01;
	u_char	unused02;
	u_int32_t flags;		/* see below */
	u_int	wire_count;		/* wired down maps refs (P) */
	int 	hold_count;		/* page hold count */

	/*
	 * NOTE that these must support one bit per DEV_BSIZE in a page!!!
	 * so, on normal X86 kernels, they must be at least 8 bits wide.
	 */
	u_char	valid;			/* map of valid DEV_BSIZE chunks */
	u_char	dirty;			/* map of dirty DEV_BSIZE chunks */

	int	ku_pagecnt;		/* kmalloc helper */
#ifdef VM_PAGE_DEBUG
	const char *busy_func;
	int	busy_line;
#endif
};

#ifndef __VM_PAGE_T_DEFINED__
#define __VM_PAGE_T_DEFINED__
typedef struct vm_page *vm_page_t;
#endif

/*
 * Page coloring parameters.  We default to a middle of the road optimization.
 * Larger selections would not really hurt us but if a machine does not have
 * a lot of memory it could cause vm_page_alloc() to eat more cpu cycles 
 * looking for free pages.
 *
 * Page coloring cannot be disabled.  Modules do not have access to most PQ
 * constants because they can change between builds.
 */
#if defined(_KERNEL) && !defined(KLD_MODULE)

#if !defined(PQ_CACHESIZE)
#define PQ_CACHESIZE 256	/* max is 1024 (MB) */
#endif

#if PQ_CACHESIZE >= 1024
#define PQ_PRIME1 31	/* Prime number somewhat less than PQ_HASH_SIZE */
#define PQ_PRIME2 23	/* Prime number somewhat less than PQ_HASH_SIZE */
#define PQ_L2_SIZE 256	/* A number of colors opt for 1M cache */

#elif PQ_CACHESIZE >= 512
#define PQ_PRIME1 31	/* Prime number somewhat less than PQ_HASH_SIZE */
#define PQ_PRIME2 23	/* Prime number somewhat less than PQ_HASH_SIZE */
#define PQ_L2_SIZE 128	/* A number of colors opt for 512K cache */

#elif PQ_CACHESIZE >= 256
#define PQ_PRIME1 13	/* Prime number somewhat less than PQ_HASH_SIZE */
#define PQ_PRIME2 7	/* Prime number somewhat less than PQ_HASH_SIZE */
#define PQ_L2_SIZE 64	/* A number of colors opt for 256K cache */

#elif PQ_CACHESIZE >= 128
#define PQ_PRIME1 9	/* Produces a good PQ_L2_SIZE/3 + PQ_PRIME1 */
#define PQ_PRIME2 5	/* Prime number somewhat less than PQ_HASH_SIZE */
#define PQ_L2_SIZE 32	/* A number of colors opt for 128k cache */

#else
#define PQ_PRIME1 5	/* Prime number somewhat less than PQ_HASH_SIZE */
#define PQ_PRIME2 3	/* Prime number somewhat less than PQ_HASH_SIZE */
#define PQ_L2_SIZE 16	/* A reasonable number of colors (opt for 64K cache) */

#endif

#define PQ_L2_MASK	(PQ_L2_SIZE - 1)

#endif /* KERNEL && !KLD_MODULE */

/*
 *
 * The queue array is always based on PQ_MAXL2_SIZE regardless of the actual
 * cache size chosen in order to present a uniform interface for modules.
 */
#define PQ_MAXL2_SIZE	256	/* fixed maximum (in pages) / module compat */

#if PQ_L2_SIZE > PQ_MAXL2_SIZE
#error "Illegal PQ_L2_SIZE"
#endif

#define PQ_NONE		0
#define PQ_FREE		1
#define PQ_INACTIVE	(1 + 1*PQ_MAXL2_SIZE)
#define PQ_ACTIVE	(2 + 1*PQ_MAXL2_SIZE)
#define PQ_CACHE	(3 + 1*PQ_MAXL2_SIZE)
#define PQ_HOLD		(3 + 2*PQ_MAXL2_SIZE)
#define PQ_COUNT	(4 + 2*PQ_MAXL2_SIZE)

/*
 * Scan support
 */
struct vm_map;

struct rb_vm_page_scan_info {
	vm_pindex_t	start_pindex;
	vm_pindex_t	end_pindex;
	int		limit;
	int		desired;
	int		error;
	int		pagerflags;
	vm_offset_t	addr;
	vm_pindex_t	backing_offset_index;
	struct vm_object *object;
	struct vm_object *backing_object;
	struct vm_page	*mpte;
	struct pmap	*pmap;
	struct vm_map	*map;
};

int rb_vm_page_scancmp(struct vm_page *, void *);

struct vpgqueues {
	struct pglist pl;
	int	*cnt;
	int	lcnt;
	int	flipflop;	/* probably not the best place */
};

extern struct vpgqueues vm_page_queues[PQ_COUNT];

/*
 * These are the flags defined for vm_page.
 *
 *  PG_UNMANAGED (used by OBJT_PHYS) indicates that the page is
 *  not under PV management but otherwise should be treated as a
 *  normal page.  Pages not under PV management cannot be paged out
 *  via the object/vm_page_t because there is no knowledge of their
 *  pte mappings, nor can they be removed from their objects via 
 *  the object, and such pages are also not on any PQ queue.  The
 *  PG_MAPPED and PG_WRITEABLE flags are not applicable.
 *
 *  PG_MAPPED only applies to managed pages, indicating whether the page
 *  is mapped onto one or more pmaps.  A page might still be mapped to
 *  special pmaps in an unmanaged fashion, for example when mapped into a
 *  buffer cache buffer, without setting PG_MAPPED.
 *
 *  PG_WRITEABLE indicates that there may be a writeable managed pmap entry
 *  somewhere, and that the page can be dirtied by hardware at any time
 *  and may have to be tested for that.  The modified bit in unmanaged
 *  mappings or in the special clean map is not tested.
 *
 *  PG_SWAPPED indicates that the page is backed by a swap block.  Any
 *  VM object type other than OBJT_DEFAULT can have swap-backed pages now.
 */
#define	PG_BUSY		0x00000001	/* page is in transit (O) */
#define	PG_WANTED	0x00000002	/* someone is waiting for page (O) */
#define PG_WINATCFLS	0x00000004	/* flush dirty page on inactive q */
#define	PG_FICTITIOUS	0x00000008	/* physical page doesn't exist (O) */
#define	PG_WRITEABLE	0x00000010	/* page is writeable */
#define PG_MAPPED	0x00000020	/* page is mapped (managed) */
#define	PG_ZERO		0x00000040	/* page is zeroed */
#define PG_REFERENCED	0x00000080	/* page has been referenced */
#define PG_CLEANCHK	0x00000100	/* page will be checked for cleaning */
#define PG_SWAPINPROG	0x00000200	/* swap I/O in progress on page	     */
#define PG_NOSYNC	0x00000400	/* do not collect for syncer */
#define PG_UNMANAGED	0x00000800	/* No PV management for page */
#define PG_MARKER	0x00001000	/* special queue marker page */
#define PG_RAM		0x00002000	/* read ahead mark */
#define PG_SWAPPED	0x00004000	/* backed by swap */
#define PG_NOTMETA	0x00008000	/* do not back with swap */
#define PG_ACTIONLIST	0x00010000	/* lookaside action list present */
	/* u_short, only 16 flag bits */

/*
 * Misc constants.
 */

#define ACT_DECLINE		1
#define ACT_ADVANCE		3
#define ACT_INIT		5
#define ACT_MAX			64

#ifdef _KERNEL
/*
 * Each pageable resident page falls into one of four lists:
 *
 *	free
 *		Available for allocation now.
 *
 * The following are all LRU sorted:
 *
 *	cache
 *		Almost available for allocation. Still in an
 *		object, but clean and immediately freeable at
 *		non-interrupt times.
 *
 *	inactive
 *		Low activity, candidates for reclamation.
 *		This is the list of pages that should be
 *		paged out next.
 *
 *	active
 *		Pages that are "active" i.e. they have been
 *		recently referenced.
 *
 *	zero
 *		Pages that are really free and have been pre-zeroed
 *
 */

extern int vm_page_zero_count;
extern struct vm_page *vm_page_array;	/* First resident page in table */
extern int vm_page_array_size;		/* number of vm_page_t's */
extern long first_page;			/* first physical page number */

#define VM_PAGE_TO_PHYS(entry)	\
		((entry)->phys_addr)

#define PHYS_TO_VM_PAGE(pa)	\
		(&vm_page_array[atop(pa) - first_page])

/*
 *	Functions implemented as macros
 */

static __inline void
vm_page_flag_set(vm_page_t m, unsigned int bits)
{
	atomic_set_int(&(m)->flags, bits);
}

static __inline void
vm_page_flag_clear(vm_page_t m, unsigned int bits)
{
	atomic_clear_int(&(m)->flags, bits);
}

#ifdef VM_PAGE_DEBUG

static __inline void
_vm_page_busy(vm_page_t m, const char *func, int lineno)
{
	ASSERT_LWKT_TOKEN_HELD(&vm_token);
	KASSERT((m->flags & PG_BUSY) == 0,
		("vm_page_busy: page already busy!!!"));
	vm_page_flag_set(m, PG_BUSY);
	m->busy_func = func;
	m->busy_line = lineno;
}

#define vm_page_busy(m)	_vm_page_busy(m, __func__, __LINE__)

#else

static __inline void
vm_page_busy(vm_page_t m)
{
	ASSERT_LWKT_TOKEN_HELD(&vm_token);
	KASSERT((m->flags & PG_BUSY) == 0, 
		("vm_page_busy: page already busy!!!"));
	vm_page_flag_set(m, PG_BUSY);
}

#endif

/*
 *	vm_page_flash:
 *
 *	wakeup anyone waiting for the page.
 */

static __inline void
vm_page_flash(vm_page_t m)
{
	lwkt_gettoken(&vm_token);
	if (m->flags & PG_WANTED) {
		vm_page_flag_clear(m, PG_WANTED);
		wakeup(m);
	}
	lwkt_reltoken(&vm_token);
}

/*
 * Clear the PG_BUSY flag and wakeup anyone waiting for the page.  This
 * is typically the last call you make on a page before moving onto
 * other things.
 */
static __inline void
vm_page_wakeup(vm_page_t m)
{
	KASSERT(m->flags & PG_BUSY, ("vm_page_wakeup: page not busy!!!"));
	vm_page_flag_clear(m, PG_BUSY);
	vm_page_flash(m);
}

/*
 * These routines manipulate the 'soft busy' count for a page.  A soft busy
 * is almost like PG_BUSY except that it allows certain compatible operations
 * to occur on the page while it is busy.  For example, a page undergoing a
 * write can still be mapped read-only.
 */
static __inline void
vm_page_io_start(vm_page_t m)
{
	atomic_add_char(&(m)->busy, 1);
}

static __inline void
vm_page_io_finish(vm_page_t m)
{
	atomic_subtract_char(&m->busy, 1);
	if (m->busy == 0)
		vm_page_flash(m);
}


#if PAGE_SIZE == 4096
#define VM_PAGE_BITS_ALL 0xff
#endif

/*
 * Note: the code will always use nominally free pages from the free list
 * before trying other flag-specified sources. 
 *
 * At least one of VM_ALLOC_NORMAL|VM_ALLOC_SYSTEM|VM_ALLOC_INTERRUPT 
 * must be specified.  VM_ALLOC_RETRY may only be specified if VM_ALLOC_NORMAL
 * is also specified.
 */
#define VM_ALLOC_NORMAL		0x01	/* ok to use cache pages */
#define VM_ALLOC_SYSTEM		0x02	/* ok to exhaust most of free list */
#define VM_ALLOC_INTERRUPT	0x04	/* ok to exhaust entire free list */
#define	VM_ALLOC_ZERO		0x08	/* req pre-zero'd memory if avail */
#define	VM_ALLOC_QUICK		0x10	/* like NORMAL but do not use cache */
#define	VM_ALLOC_RETRY		0x80	/* indefinite block (vm_page_grab()) */

void vm_page_hold(vm_page_t);
void vm_page_unhold(vm_page_t);
void vm_page_activate (vm_page_t);
vm_page_t vm_page_alloc (struct vm_object *, vm_pindex_t, int);
vm_page_t vm_page_grab (struct vm_object *, vm_pindex_t, int);
void vm_page_cache (vm_page_t);
int vm_page_try_to_cache (vm_page_t);
int vm_page_try_to_free (vm_page_t);
void vm_page_dontneed (vm_page_t);
void vm_page_deactivate (vm_page_t);
void vm_page_insert (vm_page_t, struct vm_object *, vm_pindex_t);
vm_page_t vm_page_lookup (struct vm_object *, vm_pindex_t);
void vm_page_remove (vm_page_t);
void vm_page_rename (vm_page_t, struct vm_object *, vm_pindex_t);
void vm_page_startup (void);
vm_page_t vm_add_new_page (vm_paddr_t pa);
void vm_page_unmanage (vm_page_t);
void vm_page_unwire (vm_page_t, int);
void vm_page_wire (vm_page_t);
void vm_page_unqueue (vm_page_t);
void vm_page_unqueue_nowakeup (vm_page_t);
void vm_page_set_validclean (vm_page_t, int, int);
void vm_page_set_validdirty (vm_page_t, int, int);
void vm_page_set_valid (vm_page_t, int, int);
void vm_page_set_dirty (vm_page_t, int, int);
void vm_page_clear_dirty (vm_page_t, int, int);
void vm_page_set_invalid (vm_page_t, int, int);
int vm_page_is_valid (vm_page_t, int, int);
void vm_page_test_dirty (vm_page_t);
int vm_page_bits (int, int);
vm_page_t vm_page_list_find(int basequeue, int index, boolean_t prefer_zero);
void vm_page_zero_invalid(vm_page_t m, boolean_t setvalid);
void vm_page_free_toq(vm_page_t m);
vm_page_t vm_page_free_fromq_fast(void);
vm_offset_t vm_contig_pg_kmap(int, u_long, vm_map_t, int);
void vm_contig_pg_free(int, u_long);
void vm_page_event_internal(vm_page_t, vm_page_event_t);
void vm_page_dirty(vm_page_t m);
void vm_page_register_action(vm_page_action_t action, vm_page_event_t event);
void vm_page_unregister_action(vm_page_action_t action);

/*
 * Reduce the protection of a page.  This routine never raises the 
 * protection and therefore can be safely called if the page is already
 * at VM_PROT_NONE (it will be a NOP effectively ).
 *
 * VM_PROT_NONE will remove all user mappings of a page.  This is often
 * necessary when a page changes state (for example, turns into a copy-on-write
 * page or needs to be frozen for write I/O) in order to force a fault, or
 * to force a page's dirty bits to be synchronized and avoid hardware
 * (modified/accessed) bit update races with pmap changes.
 *
 * Since 'prot' is usually a constant, this inline usually winds up optimizing
 * out the primary conditional.
 *
 * WARNING: VM_PROT_NONE can block, but will loop until all mappings have
 * been cleared.  Callers should be aware that other page related elements
 * might have changed, however.
 */
static __inline void
vm_page_protect(vm_page_t mem, int prot)
{
	if (prot == VM_PROT_NONE) {
		if (mem->flags & (PG_WRITEABLE|PG_MAPPED)) {
			pmap_page_protect(mem, VM_PROT_NONE);
			/* PG_WRITEABLE & PG_MAPPED cleared by call */
		}
	} else if ((prot == VM_PROT_READ) && (mem->flags & PG_WRITEABLE)) {
		pmap_page_protect(mem, VM_PROT_READ);
		/* PG_WRITEABLE cleared by call */
	}
}

/*
 * Zero-fill the specified page.  The entire contents of the page will be
 * zero'd out.
 */
static __inline boolean_t
vm_page_zero_fill(vm_page_t m)
{
	pmap_zero_page(VM_PAGE_TO_PHYS(m));
	return (TRUE);
}

/*
 * Copy the contents of src_m to dest_m.  The pages must be stable but spl
 * and other protections depend on context.
 */
static __inline void
vm_page_copy(vm_page_t src_m, vm_page_t dest_m)
{
	pmap_copy_page(VM_PAGE_TO_PHYS(src_m), VM_PAGE_TO_PHYS(dest_m));
	dest_m->valid = VM_PAGE_BITS_ALL;
	dest_m->dirty = VM_PAGE_BITS_ALL;
}

/*
 * Free a page.  The page must be marked BUSY.
 *
 * The clearing of PG_ZERO is a temporary safety until the code can be
 * reviewed to determine that PG_ZERO is being properly cleared on
 * write faults or maps.  PG_ZERO was previously cleared in 
 * vm_page_alloc().
 */
static __inline void
vm_page_free(vm_page_t m)
{
	vm_page_flag_clear(m, PG_ZERO);
	vm_page_free_toq(m);
}

/*
 * Free a page to the zerod-pages queue
 */
static __inline void
vm_page_free_zero(vm_page_t m)
{
#ifdef __x86_64__
	/* JG DEBUG64 We check if the page is really zeroed. */
	char *p = (char *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m));
	int i;

	for (i = 0; i < PAGE_SIZE; i++) {
		if (p[i] != 0) {
			panic("non-zero page in vm_page_free_zero()");
		}
	}

#endif
	vm_page_flag_set(m, PG_ZERO);
	vm_page_free_toq(m);
}

/*
 * Wait until page is no longer PG_BUSY or (if also_m_busy is TRUE)
 * m->busy is zero.  Returns TRUE if it had to sleep ( including if 
 * it almost had to sleep and made temporary spl*() mods), FALSE 
 * otherwise.
 *
 * This routine assumes that interrupts can only remove the busy
 * status from a page, not set the busy status or change it from
 * PG_BUSY to m->busy or vise versa (which would create a timing
 * window).
 *
 * Note: as an inline, 'also_m_busy' is usually a constant and well
 * optimized.
 */
static __inline int
vm_page_sleep_busy(vm_page_t m, int also_m_busy, const char *msg)
{
	if ((m->flags & PG_BUSY) || (also_m_busy && m->busy))  {
		lwkt_gettoken(&vm_token);
		if ((m->flags & PG_BUSY) || (also_m_busy && m->busy)) {
			/*
			 * Page is busy. Wait and retry.
			 */
			vm_page_flag_set(m, PG_WANTED | PG_REFERENCED);
			tsleep(m, 0, msg, 0);
		}
		lwkt_reltoken(&vm_token);
		return(TRUE);
		/* not reached */
	}
	return(FALSE);
}

/*
 * Set page to not be dirty.  Note: does not clear pmap modify bits .
 */
static __inline void
vm_page_undirty(vm_page_t m)
{
	m->dirty = 0;
}

#endif				/* _KERNEL */
#endif				/* !_VM_VM_PAGE_H_ */
