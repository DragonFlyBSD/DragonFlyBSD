/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
#ifndef _CPU_ATOMIC_H_
#include <machine/atomic.h>
#endif
#ifndef _VM_VM_H_
#include <vm/vm.h>
#endif
#ifndef _VM_VM_PAGE_H_
#include <vm/vm_page.h>
#endif

#ifdef _KERNEL

#ifndef _SYS_REFCOUNT_H_
#include <sys/refcount.h>
#endif

#endif

struct swblock;
struct swblock_rb_tree;
int rb_swblock_compare(struct swblock *, struct swblock *);

RB_PROTOTYPE2(swblock_rb_tree, swblock, swb_entry, rb_swblock_compare,
	      vm_pindex_t);
RB_HEAD(swblock_rb_tree, swblock);

enum obj_type { 
	OBJT_DEFAULT,
	OBJT_SWAP,   	/* object backed by swap blocks */
	OBJT_VNODE, 	/* object backed by file pages (vnode) */
	OBJT_DEVICE, 	/* object backed by device pages */
	OBJT_MGTDEVICE,
	OBJT_PHYS,  	/* object backed by physical pages */
	OBJT_DEAD,   	/* dead object */
	OBJT_MARKER	/* marker object */
};
typedef u_char objtype_t;

/*
 * A VM object which represents an arbitrarily sized data store.
 *
 * vm_objects are soft-locked with their token, meaning that any
 * blocking can allow other threads to squeeze in some work.
 */
struct vm_object {
	struct lwkt_token token;	/* everything else */
	struct lock	backing_lk;	/* lock for backing_list only */
	TAILQ_ENTRY(vm_object) object_entry;
	TAILQ_HEAD(, vm_map_backing) backing_list;
	struct vm_page_rb_tree rb_memq;	/* resident pages */
	int		generation;	/* generation ID */
	vm_pindex_t	size;		/* Object size */
	int		ref_count;
	vm_memattr_t	memattr;	/* default memory attribute for pages */
	objtype_t	type;		/* type of pager */
	u_short		flags;		/* see below */
	u_short		pg_color;	/* color of first page in obj */
	u_int		paging_in_progress;	/* Activity in progress */
	long		resident_page_count;	/* number of resident pages */
	TAILQ_ENTRY(vm_object) pager_object_entry; /* optional use by pager */
	void		*handle;	/* control handle: vp, etc */
	int		hold_count;	/* count prevents destruction */
	
#if defined(DEBUG_LOCKS)
	/* 
	 * Record threads holding a vm_object
	 */

#define VMOBJ_DEBUG_ARRAY_SIZE		(32)
	char		debug_hold_thrs[VMOBJ_DEBUG_ARRAY_SIZE][64];
	const char	*debug_hold_file[VMOBJ_DEBUG_ARRAY_SIZE];
	int		debug_hold_line[VMOBJ_DEBUG_ARRAY_SIZE];
	int		debug_index;
#endif

	union {
		/*
		 * Device pager
		 *
		 *	devp_pglist - list of allocated pages
		 */
		struct {
			TAILQ_HEAD(, vm_page) devp_pglist;
			struct cdev_pager_ops *ops;
			struct cdev *dev;
		} devp;
	} un_pager;

	/*
	 * OBJT_SWAP and OBJT_VNODE VM objects may have swap backing
	 * store.  For vnodes the swap backing store acts as a fast
	 * data cache but the vnode contains the official data.
	 */
	struct swblock_rb_tree swblock_root;
	long		swblock_count;
	struct md_object md;		/* machine specific (typ pmap) */
};

/*
 * Flags
 *
 * OBJ_ONEMAPPING - Only applies to DEFAULT and SWAP objects.  It may be
 *		    gratuitously re-cleared in other cases but will already
 *		    be clear in those cases.  It might not be set on other
 *		    object types (particularly OBJT_VNODE).
 *
 *		    This flag indicates that any given page index within the
 *		    object is only mapped to at most one vm_map_entry.
 *
 *		    WARNING!  An obj->refs of 1 does NOT allow you to
 *		    re-set this bit because the object might be part of
 *		    a shared chain of vm_map_backing structures.
 *
 * OBJ_NOPAGEIN   - vn and tmpfs set this flag, indicating to swapoff
 *		    that the objects aren't intended to have any vm_page's,
 *		    only swap blocks.  vn and tmpfs don't know how to deal
 *		    with any actual pages.
 */
#define OBJ_UNUSED0001	0x0001
#define OBJ_UNUSED0002	0x0002
#define OBJ_ACTIVE	0x0004		/* active objects */
#define OBJ_DEAD	0x0008		/* dead objects (during rundown) */
#define	OBJ_NOSPLIT	0x0010		/* dont split this object */
#define OBJ_NOPAGEIN	0x0040		/* special OBJT_SWAP */
#define	OBJ_WRITEABLE	0x0080		/* object has been made writable */
#define OBJ_MIGHTBEDIRTY 0x0100		/* object might be dirty */
#define OBJ_CLEANING	0x0200
#define OBJ_DEADWNT	0x1000		/* waiting because object is dead */
#define	OBJ_ONEMAPPING	0x2000
#define OBJ_NOMSYNC	0x4000		/* disable msync() system call */

#define IDX_TO_OFF(idx) (((vm_ooffset_t)(idx)) << PAGE_SHIFT)
#define OFF_TO_IDX(off) ((vm_pindex_t)(((vm_ooffset_t)(off)) >> PAGE_SHIFT))

#define VMOBJ_HSIZE	256
#define VMOBJ_HMASK	(VMOBJ_HSIZE - 1)

#ifdef	_KERNEL

#define OBJPC_SYNC		0x0001	/* sync I/O */
#define OBJPC_INVAL		0x0002	/* invalidate */
#define OBJPC_NOSYNC		0x0004	/* skip if PG_NOSYNC */
#define OBJPC_IGNORE_CLEANCHK	0x0008
#define OBJPC_CLUSTER_OK	0x0010	/* used only by vnode pager */
#define OBJPC_TRY_TO_CACHE	0x0020	/* typically used only in pageout path */
#define OBJPC_ALLOW_ACTIVE	0x0040	/* dito */

#if 0
/*
 * Used to chain vm_object deallocations
 */
struct vm_object_dealloc_list {
	struct vm_object_dealloc_list *next;
	vm_object_t	object;
};
#endif

TAILQ_HEAD(object_q, vm_object);

struct vm_object_hash {
	struct object_q		list;
	struct lwkt_token	token;
} __cachealign;

extern struct vm_object_hash vm_object_hash[VMOBJ_HSIZE];

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
void _vm_object_allocate (objtype_t, vm_pindex_t, vm_object_t, const char *);
boolean_t vm_object_coalesce (vm_object_t, vm_pindex_t, vm_size_t, vm_size_t);
vm_object_t vm_object_collapse (vm_object_t, vm_object_t);
void vm_object_terminate (vm_object_t);
void vm_object_set_writeable_dirty (vm_object_t);
void vm_object_init(vm_object_t, vm_pindex_t);
void vm_object_init1 (void);
void vm_object_page_clean (vm_object_t, vm_pindex_t, vm_pindex_t, int);
void vm_object_page_remove (vm_object_t, vm_pindex_t, vm_pindex_t, boolean_t);
void vm_object_pmap_copy (vm_object_t, vm_pindex_t, vm_pindex_t);
void vm_object_madvise (vm_object_t, vm_pindex_t, vm_pindex_t, int);
void vm_object_init2 (void);
vm_page_t vm_fault_object_page(vm_object_t, vm_ooffset_t,
				vm_prot_t, int, int *, int *);
void vm_object_lock_swap(void);
void vm_object_lock(vm_object_t);
void vm_object_lock_shared(vm_object_t);
void vm_object_unlock(vm_object_t);

#if defined(DEBUG_LOCKS)

#define VMOBJDEBUG(x)	debug ## x
#define VMOBJDBARGS	, char *file, int line
#define VMOBJDBFWD	, file, line

#define vm_object_hold(obj)			\
		debugvm_object_hold(obj, __FILE__, __LINE__)
#define vm_object_hold_try(obj)			\
		debugvm_object_hold_try(obj, __FILE__, __LINE__)
#define vm_object_hold_shared(obj)		\
		debugvm_object_hold_shared(obj, __FILE__, __LINE__)
#define vm_object_drop(obj)			\
		debugvm_object_drop(obj, __FILE__, __LINE__)
#define vm_object_reference_quick(obj)		\
		debugvm_object_reference_quick(obj, __FILE__, __LINE__)
#define vm_object_reference_locked(obj)		\
		debugvm_object_reference_locked(obj, __FILE__, __LINE__)
#if 0
#define vm_object_reference_locked_chain_held(obj)		\
		debugvm_object_reference_locked_chain_held(	\
					obj, __FILE__, __LINE__)
#endif
#define vm_object_deallocate(obj)		\
		debugvm_object_deallocate(obj, __FILE__, __LINE__)
#define vm_object_deallocate_locked(obj)	\
		debugvm_object_deallocate_locked(obj, __FILE__, __LINE__)

#else

#define VMOBJDEBUG(x)	x
#define VMOBJDBARGS
#define VMOBJDBFWD

#endif

void VMOBJDEBUG(vm_object_hold)(vm_object_t object VMOBJDBARGS);
int VMOBJDEBUG(vm_object_hold_try)(vm_object_t object VMOBJDBARGS);
void VMOBJDEBUG(vm_object_hold_shared)(vm_object_t object VMOBJDBARGS);
void VMOBJDEBUG(vm_object_drop)(vm_object_t object VMOBJDBARGS);
void VMOBJDEBUG(vm_object_reference_quick)(vm_object_t object VMOBJDBARGS);
void VMOBJDEBUG(vm_object_reference_locked)(vm_object_t object VMOBJDBARGS);
#if 0
void VMOBJDEBUG(vm_object_reference_locked_chain_held)(
			vm_object_t object VMOBJDBARGS);
#endif
void VMOBJDEBUG(vm_object_deallocate)(vm_object_t object VMOBJDBARGS);
void VMOBJDEBUG(vm_object_deallocate_locked)(vm_object_t object VMOBJDBARGS);

void vm_object_upgrade(vm_object_t);
void vm_object_downgrade(vm_object_t);
int vm_quickcolor(void);

#endif				/* _KERNEL */

#endif				/* _VM_VM_OBJECT_H_ */
