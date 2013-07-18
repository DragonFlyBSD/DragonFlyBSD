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
 *	from: @(#)vm_object.h	8.3 (Berkeley) 1/12/94
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
 * $FreeBSD: src/sys/vm/vm_object.h,v 1.63.2.3 2003/05/26 19:17:56 alc Exp $
 */

/*
 *	Virtual memory object module definitions.
 */

#ifndef	_VM_VM_OBJECT_H_
#define	_VM_VM_OBJECT_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#if defined(_KERNEL) && !defined(_SYS_SYSTM_H_)
#include <sys/systm.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _MACHINE_PMAP_H_
#include <machine/pmap.h>
#endif
#ifndef _MACHINE_ATOMIC_H_
#include <machine/atomic.h>
#endif
#ifndef _VM_VM_H_
#include <vm/vm.h>
#endif
#ifndef _VM_VM_PAGE_H_
#include <vm/vm_page.h>
#endif

#ifdef _KERNEL

#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>
#endif

#ifndef _SYS_REFCOUNT_H_
#include <sys/refcount.h>
#endif

#endif

struct swblock;
struct swblock_rb_tree;
int rb_swblock_compare(struct swblock *, struct swblock *);

RB_PROTOTYPE2(swblock_rb_tree, swblock, swb_entry, rb_swblock_compare,
	      vm_pindex_t);

enum obj_type { 
	OBJT_DEFAULT,
	OBJT_SWAP,   	/* object backed by swap blocks */
	OBJT_VNODE, 	/* object backed by file pages (vnode) */
	OBJT_DEVICE, 	/* object backed by device pages */
	OBJT_PHYS,  	/* object backed by physical pages */
	OBJT_DEAD,   	/* dead object */
	OBJT_MARKER	/* marker object */
};
typedef u_char objtype_t;

/*
 * vm_object		A VM object which represents an arbitrarily sized
 *			data store.
 *
 * Locking requirements:
 *	vmobj_token for object_list
 *
 *	vm_object_hold/drop() for most vm_object related operations.
 *
 *	OBJ_CHAINLOCK to avoid chain/shadow object collisions
 */
struct vm_object {
	TAILQ_ENTRY(vm_object) object_list; /* vmobj_token */
	LIST_HEAD(, vm_object) shadow_head; /* objects we are a shadow for */
	LIST_ENTRY(vm_object) shadow_list;  /* chain of shadow objects */
	RB_HEAD(vm_page_rb_tree, vm_page) rb_memq;	/* resident pages */
	int generation;			/* generation ID */
	vm_pindex_t size;		/* Object size */
	int ref_count;
	int shadow_count;		/* count of objs we are a shadow for */
	uint8_t pat_mode;
	uint8_t unused01;
	uint8_t unused02;
	uint8_t unused03;
	objtype_t type;			/* type of pager */
	u_short flags;			/* see below */
	u_short pg_color;		/* color of first page in obj */
	u_int paging_in_progress;	/* Paging (in or out) so don't collapse or destroy */
	int resident_page_count;	/* number of resident pages */
        u_int agg_pv_list_count;        /* aggregate pv list count */
	struct vm_object *backing_object; /* object that I'm a shadow of */
	vm_ooffset_t backing_object_offset;/* Offset in backing object */
	void *handle;			/* control handle: vp, etc */
	int hold_count;			/* count prevents destruction */
	
#if defined(DEBUG_LOCKS)
	/* 
	 * Record threads holding a vm_object
	 */

#define VMOBJ_DEBUG_ARRAY_SIZE		(32)
	u_int debug_hold_bitmap;
	thread_t debug_hold_thrs[VMOBJ_DEBUG_ARRAY_SIZE];
	char *debug_hold_file[VMOBJ_DEBUG_ARRAY_SIZE];
	int debug_hold_line[VMOBJ_DEBUG_ARRAY_SIZE];
	u_int debug_hold_ovfl;
#endif

	union {
		/*
		 * Device pager
		 *
		 *	devp_pglist - list of allocated pages
		 */
		struct {
			TAILQ_HEAD(, vm_page) devp_pglist;
		} devp;
	} un_pager;

	/*
	 * OBJT_SWAP and OBJT_VNODE VM objects may have swap backing
	 * store.  For vnodes the swap backing store acts as a fast
	 * data cache but the vnode contains the official data.
	 */
	RB_HEAD(swblock_rb_tree, swblock) swblock_root;
	int	swblock_count;
	struct	lwkt_token	token;
	struct md_object	md;	/* machine specific (typ pmap) */
};

/*
 * Flags
 *
 * NOTE: OBJ_ONEMAPPING only applies to DEFAULT and SWAP objects.  It
 *	 may be gratuitously re-cleared in other cases but will already be
 *	 clear in those cases.
 */
#define OBJ_CHAINLOCK	0x0001		/* backing_object/shadow changing */
#define OBJ_CHAINWANT	0x0002
#define OBJ_ACTIVE	0x0004		/* active objects */
#define OBJ_DEAD	0x0008		/* dead objects (during rundown) */
#define	OBJ_NOSPLIT	0x0010		/* dont split this object */
#define OBJ_UNUSED0040	0x0040
#define	OBJ_WRITEABLE	0x0080		/* object has been made writable */
#define OBJ_MIGHTBEDIRTY 0x0100		/* object might be dirty */
#define OBJ_CLEANING	0x0200
#define OBJ_DEADWNT	0x1000		/* waiting because object is dead */
#define	OBJ_ONEMAPPING	0x2000		/* flag single vm_map_entry mapping */
#define OBJ_NOMSYNC	0x4000		/* disable msync() system call */

#define IDX_TO_OFF(idx) (((vm_ooffset_t)(idx)) << PAGE_SHIFT)
#define OFF_TO_IDX(off) ((vm_pindex_t)(((vm_ooffset_t)(off)) >> PAGE_SHIFT))

#ifdef	_KERNEL

#define OBJPC_SYNC	0x1			/* sync I/O */
#define OBJPC_INVAL	0x2			/* invalidate */
#define OBJPC_NOSYNC	0x4			/* skip if PG_NOSYNC */

/*
 * Used to chain vm_object deallocations
 */
struct vm_object_dealloc_list {
	struct vm_object_dealloc_list *next;
	vm_object_t	object;
};

TAILQ_HEAD(object_q, vm_object);

extern struct object_q vm_object_list;	/* list of allocated objects */

 /* lock for object list and count */

extern struct vm_object kernel_object;	/* the single kernel object */
extern int vm_shared_fault;

#endif				/* _KERNEL */

#ifdef _KERNEL

#define VM_OBJECT_LOCK(object)		vm_object_hold(object)
#define VM_OBJECT_UNLOCK(object)	vm_object_drop(object)

static __inline void
vm_object_set_flag(vm_object_t object, u_int bits)
{
	atomic_set_short(&object->flags, bits);
}

static __inline void
vm_object_clear_flag(vm_object_t object, u_int bits)
{
	atomic_clear_short(&object->flags, bits);
}

static __inline void
vm_object_pip_add(vm_object_t object, u_int i)
{
	refcount_acquire_n(&object->paging_in_progress, i);
}

static __inline void
vm_object_pip_wakeup_n(vm_object_t object, u_int i)
{
	refcount_release_wakeup_n(&object->paging_in_progress, i);
}

static __inline void
vm_object_pip_wakeup(vm_object_t object)
{
	vm_object_pip_wakeup_n(object, 1);
}

static __inline void
vm_object_pip_wait(vm_object_t object, char *waitid)
{
	refcount_wait(&object->paging_in_progress, waitid);
}

static __inline lwkt_token_t
vm_object_token(vm_object_t obj)
{
	return (&obj->token);
}

vm_object_t vm_object_allocate (objtype_t, vm_pindex_t);
vm_object_t vm_object_allocate_hold (objtype_t, vm_pindex_t);
void _vm_object_allocate (objtype_t, vm_pindex_t, vm_object_t);
boolean_t vm_object_coalesce (vm_object_t, vm_pindex_t, vm_size_t, vm_size_t);
void vm_object_collapse (vm_object_t, struct vm_object_dealloc_list **);
void vm_object_deallocate (vm_object_t);
void vm_object_deallocate_locked (vm_object_t);
void vm_object_deallocate_list(struct vm_object_dealloc_list **);
void vm_object_terminate (vm_object_t);
void vm_object_set_writeable_dirty (vm_object_t);
void vm_object_init (void);
void vm_object_page_clean (vm_object_t, vm_pindex_t, vm_pindex_t, boolean_t);
void vm_object_page_remove (vm_object_t, vm_pindex_t, vm_pindex_t, boolean_t);
void vm_object_pmap_copy (vm_object_t, vm_pindex_t, vm_pindex_t);
void vm_object_pmap_copy_1 (vm_object_t, vm_pindex_t, vm_pindex_t);
void vm_object_pmap_remove (vm_object_t, vm_pindex_t, vm_pindex_t);
void vm_object_reference_locked (vm_object_t);
void vm_object_chain_wait (vm_object_t);
void vm_object_chain_acquire(vm_object_t object);
void vm_object_chain_release(vm_object_t object);
void vm_object_chain_release_all(vm_object_t object, vm_object_t stopobj);
void vm_object_shadow (vm_object_t *, vm_ooffset_t *, vm_size_t, int);
void vm_object_madvise (vm_object_t, vm_pindex_t, int, int);
void vm_object_init2 (void);
vm_page_t vm_fault_object_page(vm_object_t, vm_ooffset_t,
				vm_prot_t, int, int, int *);
void vm_object_dead_sleep(vm_object_t, const char *);
void vm_object_dead_wakeup(vm_object_t);
void vm_object_lock_swap(void);
void vm_object_lock(vm_object_t);
void vm_object_lock_shared(vm_object_t);
void vm_object_unlock(vm_object_t);

#ifndef DEBUG_LOCKS
void vm_object_hold(vm_object_t);
int vm_object_hold_maybe_shared(vm_object_t);
int vm_object_hold_try(vm_object_t);
void vm_object_hold_shared(vm_object_t);
#else
#define vm_object_hold_maybe_shared(obj)		\
	debugvm_object_hold_maybe_shared(obj, __FILE__, __LINE__)
int debugvm_object_hold_maybe_shared(vm_object_t, char *, int);
#define vm_object_hold(obj)		\
	debugvm_object_hold(obj, __FILE__, __LINE__)
void debugvm_object_hold(vm_object_t, char *, int);
#define vm_object_hold_try(obj)		\
	debugvm_object_hold_try(obj, __FILE__, __LINE__)
int debugvm_object_hold_try(vm_object_t, char *, int);
#define vm_object_hold_shared(obj)	\
	debugvm_object_hold_shared(obj, __FILE__, __LINE__)
void debugvm_object_hold_shared(vm_object_t, char *, int);
#endif

void vm_object_drop(vm_object_t);

#endif				/* _KERNEL */

#endif				/* _VM_VM_OBJECT_H_ */
