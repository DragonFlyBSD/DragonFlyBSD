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
#include <machine/atomic.h>
#ifndef _VM_VM_H_
#include <vm/vm.h>
#endif
#ifndef _VM_VM_PAGE_H_
#include <vm/vm_page.h>
#endif

#ifdef _KERNEL

#ifndef _SYS_THREAD_H
#include <sys/thread.h>
#endif

#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>
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
 * Locking requirements: vmobj_token for ref_count and object_list, and
 * vm_token for everything else.
 */
struct vm_object {
	TAILQ_ENTRY(vm_object) object_list; /* vmobj_token */
	LIST_HEAD(, vm_object) shadow_head; /* objects that this is a shadow for */
	LIST_ENTRY(vm_object) shadow_list; /* chain of shadow objects */
	RB_HEAD(vm_page_rb_tree, vm_page) rb_memq;	/* resident pages */
	int generation;			/* generation ID */
	vm_pindex_t size;		/* Object size */
	int ref_count;			/* vmobj_token */
	int shadow_count;		/* how many objects that this is a shadow for */
	objtype_t type;			/* type of pager */
	u_short flags;			/* see below */
	u_short pg_color;		/* color of first page in obj */
	int paging_in_progress;		/* Paging (in or out) so don't collapse or destroy */
	int resident_page_count;	/* number of resident pages */
        u_int agg_pv_list_count;        /* aggregate pv list count */
	struct vm_object *backing_object; /* object that I'm a shadow of */
	vm_ooffset_t backing_object_offset;/* Offset in backing object */
	void *handle;
	int hold_count;			/* refcount for object liveness */
	
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
};

/*
 * Flags
 */
#define OBJ_ACTIVE	0x0004		/* active objects */
#define OBJ_DEAD	0x0008		/* dead objects (during rundown) */
#define	OBJ_NOSPLIT	0x0010		/* dont split this object */
#define OBJ_PIPWNT	0x0040		/* paging in progress wanted */
#define	OBJ_WRITEABLE	0x0080		/* object has been made writable */
#define OBJ_MIGHTBEDIRTY 0x0100		/* object might be dirty */
#define OBJ_CLEANING	0x0200
#define OBJ_DEADWNT	0x1000		/* waiting because object is dead */
#define	OBJ_ONEMAPPING	0x2000		/* One USE (a single, non-forked) mapping flag */
#define OBJ_NOMSYNC	0x4000		/* disable msync() system call */

#define IDX_TO_OFF(idx) (((vm_ooffset_t)(idx)) << PAGE_SHIFT)
#define OFF_TO_IDX(off) ((vm_pindex_t)(((vm_ooffset_t)(off)) >> PAGE_SHIFT))

#ifdef	_KERNEL

#define OBJPC_SYNC	0x1			/* sync I/O */
#define OBJPC_INVAL	0x2			/* invalidate */
#define OBJPC_NOSYNC	0x4			/* skip if PG_NOSYNC */

TAILQ_HEAD(object_q, vm_object);

extern struct object_q vm_object_list;	/* list of allocated objects */

 /* lock for object list and count */

extern struct vm_object kernel_object;	/* the single kernel object */

#endif				/* _KERNEL */

#ifdef _KERNEL

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
vm_object_pip_add(vm_object_t object, int i)
{
	atomic_add_int(&object->paging_in_progress, i);
}

static __inline void
vm_object_pip_subtract(vm_object_t object, int i)
{
	atomic_subtract_int(&object->paging_in_progress, i);
}

static __inline void
vm_object_pip_wakeupn(vm_object_t object, int i)
{
	if (i)
		atomic_subtract_int(&object->paging_in_progress, i);
	if ((object->flags & OBJ_PIPWNT) && object->paging_in_progress == 0) {
		vm_object_clear_flag(object, OBJ_PIPWNT);
		wakeup(object);
	}
}

static __inline void
vm_object_pip_wakeup(vm_object_t object)
{
	vm_object_pip_wakeupn(object, 1);
}

static __inline void
vm_object_pip_sleep(vm_object_t object, char *waitid)
{
	if (object->paging_in_progress) {
		crit_enter();
		if (object->paging_in_progress) {
			vm_object_set_flag(object, OBJ_PIPWNT);
			tsleep(object, 0, waitid, 0);
		}
		crit_exit();
	}
}

static __inline void
vm_object_pip_wait(vm_object_t object, char *waitid)
{
	while (object->paging_in_progress)
		vm_object_pip_sleep(object, waitid);
}

vm_object_t vm_object_allocate (objtype_t, vm_pindex_t);
void _vm_object_allocate (objtype_t, vm_pindex_t, vm_object_t);
boolean_t vm_object_coalesce (vm_object_t, vm_pindex_t, vm_size_t, vm_size_t);
void vm_object_collapse (vm_object_t);
void vm_object_deallocate (vm_object_t);
void vm_object_deallocate_locked (vm_object_t);
void vm_object_terminate (vm_object_t);
void vm_object_set_writeable_dirty (vm_object_t);
void vm_object_init (void);
void vm_object_page_clean (vm_object_t, vm_pindex_t, vm_pindex_t, boolean_t);
void vm_object_page_remove (vm_object_t, vm_pindex_t, vm_pindex_t, boolean_t);
void vm_object_pmap_copy (vm_object_t, vm_pindex_t, vm_pindex_t);
void vm_object_pmap_copy_1 (vm_object_t, vm_pindex_t, vm_pindex_t);
void vm_object_pmap_remove (vm_object_t, vm_pindex_t, vm_pindex_t);
void vm_object_reference (vm_object_t);
void vm_object_reference_locked (vm_object_t);
void vm_object_shadow (vm_object_t *, vm_ooffset_t *, vm_size_t);
void vm_object_madvise (vm_object_t, vm_pindex_t, int, int);
void vm_object_init2 (void);
vm_page_t vm_fault_object_page(vm_object_t, vm_ooffset_t, vm_prot_t, int, int *);
void vm_object_dead_sleep(vm_object_t, const char *);
void vm_object_dead_wakeup(vm_object_t);
void vm_object_lock(vm_object_t);
void vm_object_unlock(vm_object_t);
void vm_object_hold(vm_object_t);
void vm_object_drop(vm_object_t);

#endif				/* _KERNEL */

#endif				/* _VM_VM_OBJECT_H_ */
