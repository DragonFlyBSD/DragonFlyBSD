/*
 * Copyright (c) 1990 University of Utah.
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 *	from: @(#)swap_pager.h	7.1 (Berkeley) 12/5/90
 * $FreeBSD: src/sys/vm/swap_pager.h,v 1.28.2.1 2000/10/13 07:13:23 dillon Exp $
 */

/*
 * Modifications to the block allocation data structure by John S. Dyson
 * 18 Dec 93.
 */

#ifndef	_VM_SWAP_PAGER_H_
#define	_VM_SWAP_PAGER_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _VM_VM_OBJECT_H_
#include <vm/vm_object.h>
#endif
#ifndef _SYS_BLIST_H_
#include <sys/blist.h>
#endif

/*
 * SWB_NPAGES must be a power of 2.  It may be set to 1, 2, 4, 8, or 16
 * pages per allocation.  We recommend you stick with the default of 8.
 * The 16-page limit is due to the radix code (kern/subr_blist.c).
 */
#define SWB_NPAGES	16

/*
 * Piecemeal swap metadata structure.  Swap is stored in a hash table.
 *
 * Storage use is ~1:16384 or so.
 *
 * Overall memory utilization is about the same as the old swap structure.
 */

#define SWAP_META_PAGES		(SWB_NPAGES * 2)
#define SWAP_META_MASK		(SWAP_META_PAGES - 1)

struct swblock {
	RB_ENTRY(swblock) swb_entry;
	vm_pindex_t	swb_index;
	int		swb_count;
	swblk_t		swb_pages[SWAP_META_PAGES];
};

#ifdef _KERNEL
extern int swap_pager_full;
extern int vm_swap_size;
extern int vm_swap_max;
extern int vm_swap_cache_use;
extern int vm_swap_anon_use;
extern int vm_swapcache_read_enable;
extern int vm_swapcache_inactive_heuristic;
extern int vm_swapcache_use_chflags;

extern struct blist *swapblist;
extern int nswap_lowat, nswap_hiwat;

void swap_pager_putpages (vm_object_t, struct vm_page **, int, int, int *);
boolean_t swap_pager_haspage (vm_object_t object, vm_pindex_t pindex);
int swap_pager_swapoff (int devidx);

int swap_pager_swp_alloc (vm_object_t, int);
void swap_pager_copy (vm_object_t, vm_object_t, vm_pindex_t, int);
void swap_pager_freespace (vm_object_t, vm_pindex_t, vm_pindex_t);
void swap_pager_freespace_all (vm_object_t);
int swap_pager_condfree(vm_object_t, vm_pindex_t *, int);

void swap_pager_page_inserted(vm_page_t);
void swap_pager_swap_init (void);
void swap_pager_newswap (void);
int swap_pager_reserve (vm_object_t, vm_pindex_t, vm_size_t);

void swapacctspace(swblk_t base, swblk_t count);

/*
 * newswap functions
 */

void swap_pager_page_removed (struct vm_page *, vm_object_t);

/* choose underlying swap device and queue up I/O */
struct buf;
void swstrategy (struct buf *bp);	/* probably needs to move elsewhere */

#endif

#endif				/* _VM_SWAP_PAGER_H_ */
