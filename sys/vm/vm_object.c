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
 *	from: @(#)vm_object.c	8.5 (Berkeley) 3/22/94
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
 * $FreeBSD: src/sys/vm/vm_object.c,v 1.171.2.8 2003/05/26 19:17:56 alc Exp $
 */

/*
 *	Virtual memory object module.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>		/* for curproc, pageproc */
#include <sys/thread.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/refcount.h>

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

#include <vm/vm_page2.h>

#include <machine/specialreg.h>

#define EASY_SCAN_FACTOR	8

static void	vm_object_qcollapse(vm_object_t object,
				    vm_object_t backing_object);
static void	vm_object_page_collect_flush(vm_object_t object, vm_page_t p,
					     int pagerflags);
static void	vm_object_lock_init(vm_object_t);


/*
 *	Virtual memory objects maintain the actual data
 *	associated with allocated virtual memory.  A given
 *	page of memory exists within exactly one object.
 *
 *	An object is only deallocated when all "references"
 *	are given up.  Only one "reference" to a given
 *	region of an object should be writeable.
 *
 *	Associated with each object is a list of all resident
 *	memory pages belonging to that object; this list is
 *	maintained by the "vm_page" module, and locked by the object's
 *	lock.
 *
 *	Each object also records a "pager" routine which is
 *	used to retrieve (and store) pages to the proper backing
 *	storage.  In addition, objects may be backed by other
 *	objects from which they were virtual-copied.
 *
 *	The only items within the object structure which are
 *	modified after time of creation are:
 *		reference count		locked by object's lock
 *		pager routine		locked by object's lock
 *
 */

struct object_q vm_object_list;		/* locked by vmobj_token */
struct vm_object kernel_object;

static long vm_object_count;		/* locked by vmobj_token */

static long object_collapses;
static long object_bypasses;
static int next_index;
static vm_zone_t obj_zone;
static struct vm_zone obj_zone_store;
#define VM_OBJECTS_INIT 256
static struct vm_object vm_objects_init[VM_OBJECTS_INIT];

/*
 * Misc low level routines
 */
static void
vm_object_lock_init(vm_object_t obj)
{
#if defined(DEBUG_LOCKS)
	int i;

	obj->debug_hold_bitmap = 0;
	obj->debug_hold_ovfl = 0;
	for (i = 0; i < VMOBJ_DEBUG_ARRAY_SIZE; i++) {
		obj->debug_hold_thrs[i] = NULL;
		obj->debug_hold_file[i] = NULL;
		obj->debug_hold_line[i] = 0;
	}
#endif
}

void
vm_object_lock_swap(void)
{
	lwkt_token_swap();
}

void
vm_object_lock(vm_object_t obj)
{
	lwkt_gettoken(&obj->token);
}

/*
 * Returns TRUE on sucesss
 */
static int
vm_object_lock_try(vm_object_t obj)
{
	return(lwkt_trytoken(&obj->token));
}

void
vm_object_lock_shared(vm_object_t obj)
{
	lwkt_gettoken_shared(&obj->token);
}

void
vm_object_unlock(vm_object_t obj)
{
	lwkt_reltoken(&obj->token);
}

static __inline void
vm_object_assert_held(vm_object_t obj)
{
	ASSERT_LWKT_TOKEN_HELD(&obj->token);
}

void
#ifndef DEBUG_LOCKS
vm_object_hold(vm_object_t obj)
#else
debugvm_object_hold(vm_object_t obj, char *file, int line)
#endif
{
	KKASSERT(obj != NULL);

	/*
	 * Object must be held (object allocation is stable due to callers
	 * context, typically already holding the token on a parent object)
	 * prior to potentially blocking on the lock, otherwise the object
	 * can get ripped away from us.
	 */
	refcount_acquire(&obj->hold_count);
	vm_object_lock(obj);

#if defined(DEBUG_LOCKS)
	int i;
	u_int mask;

	for (;;) {
		mask = ~obj->debug_hold_bitmap;
		cpu_ccfence();
		if (mask == 0xFFFFFFFFU) {
			if (obj->debug_hold_ovfl == 0)
				obj->debug_hold_ovfl = 1;
			break;
		}
		i = ffs(mask) - 1;
		if (atomic_cmpset_int(&obj->debug_hold_bitmap, ~mask,
				      ~mask | (1 << i))) {
			obj->debug_hold_bitmap |= (1 << i);
			obj->debug_hold_thrs[i] = curthread;
			obj->debug_hold_file[i] = file;
			obj->debug_hold_line[i] = line;
			break;
		}
	}
#endif
}

int
#ifndef DEBUG_LOCKS
vm_object_hold_try(vm_object_t obj)
#else
debugvm_object_hold_try(vm_object_t obj, char *file, int line)
#endif
{
	KKASSERT(obj != NULL);

	/*
	 * Object must be held (object allocation is stable due to callers
	 * context, typically already holding the token on a parent object)
	 * prior to potentially blocking on the lock, otherwise the object
	 * can get ripped away from us.
	 */
	refcount_acquire(&obj->hold_count);
	if (vm_object_lock_try(obj) == 0) {
		if (refcount_release(&obj->hold_count)) {
			if (obj->ref_count == 0 && (obj->flags & OBJ_DEAD))
				zfree(obj_zone, obj);
		}
		return(0);
	}

#if defined(DEBUG_LOCKS)
	int i;
	u_int mask;

	for (;;) {
		mask = ~obj->debug_hold_bitmap;
		cpu_ccfence();
		if (mask == 0xFFFFFFFFU) {
			if (obj->debug_hold_ovfl == 0)
				obj->debug_hold_ovfl = 1;
			break;
		}
		i = ffs(mask) - 1;
		if (atomic_cmpset_int(&obj->debug_hold_bitmap, ~mask,
				      ~mask | (1 << i))) {
			obj->debug_hold_bitmap |= (1 << i);
			obj->debug_hold_thrs[i] = curthread;
			obj->debug_hold_file[i] = file;
			obj->debug_hold_line[i] = line;
			break;
		}
	}
#endif
	return(1);
}

void
#ifndef DEBUG_LOCKS
vm_object_hold_shared(vm_object_t obj)
#else
debugvm_object_hold_shared(vm_object_t obj, char *file, int line)
#endif
{
	KKASSERT(obj != NULL);

	/*
	 * Object must be held (object allocation is stable due to callers
	 * context, typically already holding the token on a parent object)
	 * prior to potentially blocking on the lock, otherwise the object
	 * can get ripped away from us.
	 */
	refcount_acquire(&obj->hold_count);
	vm_object_lock_shared(obj);

#if defined(DEBUG_LOCKS)
	int i;
	u_int mask;

	for (;;) {
		mask = ~obj->debug_hold_bitmap;
		cpu_ccfence();
		if (mask == 0xFFFFFFFFU) {
			if (obj->debug_hold_ovfl == 0)
				obj->debug_hold_ovfl = 1;
			break;
		}
		i = ffs(mask) - 1;
		if (atomic_cmpset_int(&obj->debug_hold_bitmap, ~mask,
				      ~mask | (1 << i))) {
			obj->debug_hold_bitmap |= (1 << i);
			obj->debug_hold_thrs[i] = curthread;
			obj->debug_hold_file[i] = file;
			obj->debug_hold_line[i] = line;
			break;
		}
	}
#endif
}

/*
 * Obtain either a shared or exclusive lock on VM object
 * based on whether this is a terminal vnode object or not.
 */
int
#ifndef DEBUG_LOCKS
vm_object_hold_maybe_shared(vm_object_t obj)
#else
debugvm_object_hold_maybe_shared(vm_object_t obj, char *file, int line)
#endif
{
	if (vm_shared_fault &&
	    obj->type == OBJT_VNODE &&
	    obj->backing_object == NULL) {
		vm_object_hold_shared(obj);
		return(1);
	} else {
		vm_object_hold(obj);
		return(0);
	}
}

/*
 * Drop the token and hold_count on the object.
 */
void
vm_object_drop(vm_object_t obj)
{
	if (obj == NULL)
		return;

#if defined(DEBUG_LOCKS)
	int found = 0;
	int i;

	for (i = 0; i < VMOBJ_DEBUG_ARRAY_SIZE; i++) {
		if ((obj->debug_hold_bitmap & (1 << i)) &&
		    (obj->debug_hold_thrs[i] == curthread)) {
			obj->debug_hold_bitmap &= ~(1 << i);
			obj->debug_hold_thrs[i] = NULL;
			obj->debug_hold_file[i] = NULL;
			obj->debug_hold_line[i] = 0;
			found = 1;
			break;
		}
	}

	if (found == 0 && obj->debug_hold_ovfl == 0)
		panic("vm_object: attempt to drop hold on non-self-held obj");
#endif

	/*
	 * No new holders should be possible once we drop hold_count 1->0 as
	 * there is no longer any way to reference the object.
	 */
	KKASSERT(obj->hold_count > 0);
	if (refcount_release(&obj->hold_count)) {
		if (obj->ref_count == 0 && (obj->flags & OBJ_DEAD)) {
			vm_object_unlock(obj);
			zfree(obj_zone, obj);
		} else {
			vm_object_unlock(obj);
		}
	} else {
		vm_object_unlock(obj);
	}
}

/*
 * Initialize a freshly allocated object, returning a held object.
 *
 * Used only by vm_object_allocate() and zinitna().
 *
 * No requirements.
 */
void
_vm_object_allocate(objtype_t type, vm_pindex_t size, vm_object_t object)
{
	int incr;

	RB_INIT(&object->rb_memq);
	LIST_INIT(&object->shadow_head);
	lwkt_token_init(&object->token, "vmobj");

	object->type = type;
	object->size = size;
	object->ref_count = 1;
	object->memattr = VM_MEMATTR_DEFAULT;
	object->hold_count = 0;
	object->flags = 0;
	if ((object->type == OBJT_DEFAULT) || (object->type == OBJT_SWAP))
		vm_object_set_flag(object, OBJ_ONEMAPPING);
	object->paging_in_progress = 0;
	object->resident_page_count = 0;
	object->agg_pv_list_count = 0;
	object->shadow_count = 0;
	/* cpu localization twist */
	object->pg_color = (int)(intptr_t)curthread;
	if ( size > (PQ_L2_SIZE / 3 + PQ_PRIME1))
		incr = PQ_L2_SIZE / 3 + PQ_PRIME1;
	else
		incr = size;
	next_index = (next_index + incr) & PQ_L2_MASK;
	object->handle = NULL;
	object->backing_object = NULL;
	object->backing_object_offset = (vm_ooffset_t)0;

	object->generation++;
	object->swblock_count = 0;
	RB_INIT(&object->swblock_root);
	vm_object_lock_init(object);
	pmap_object_init(object);

	vm_object_hold(object);
	lwkt_gettoken(&vmobj_token);
	TAILQ_INSERT_TAIL(&vm_object_list, object, object_list);
	vm_object_count++;
	lwkt_reltoken(&vmobj_token);
}

/*
 * Initialize the VM objects module.
 *
 * Called from the low level boot code only.
 */
void
vm_object_init(void)
{
	TAILQ_INIT(&vm_object_list);
	
	_vm_object_allocate(OBJT_DEFAULT, OFF_TO_IDX(KvaEnd),
			    &kernel_object);
	vm_object_drop(&kernel_object);

	obj_zone = &obj_zone_store;
	zbootinit(obj_zone, "VM OBJECT", sizeof (struct vm_object),
		vm_objects_init, VM_OBJECTS_INIT);
}

void
vm_object_init2(void)
{
	zinitna(obj_zone, NULL, NULL, 0, 0, ZONE_PANICFAIL, 1);
}

/*
 * Allocate and return a new object of the specified type and size.
 *
 * No requirements.
 */
vm_object_t
vm_object_allocate(objtype_t type, vm_pindex_t size)
{
	vm_object_t result;

	result = (vm_object_t) zalloc(obj_zone);

	_vm_object_allocate(type, size, result);
	vm_object_drop(result);

	return (result);
}

/*
 * This version returns a held object, allowing further atomic initialization
 * of the object.
 */
vm_object_t
vm_object_allocate_hold(objtype_t type, vm_pindex_t size)
{
	vm_object_t result;

	result = (vm_object_t) zalloc(obj_zone);

	_vm_object_allocate(type, size, result);

	return (result);
}

/*
 * Add an additional reference to a vm_object.  The object must already be
 * held.  The original non-lock version is no longer supported.  The object
 * must NOT be chain locked by anyone at the time the reference is added.
 *
 * Referencing a chain-locked object can blow up the fairly sensitive
 * ref_count and shadow_count tests in the deallocator.  Most callers
 * will call vm_object_chain_wait() prior to calling
 * vm_object_reference_locked() to avoid the case.
 *
 * The object must be held, but may be held shared if desired (hence why
 * we use an atomic op).
 */
void
vm_object_reference_locked(vm_object_t object)
{
	KKASSERT(object != NULL);
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	KKASSERT((object->flags & OBJ_CHAINLOCK) == 0);
	atomic_add_int(&object->ref_count, 1);
	if (object->type == OBJT_VNODE) {
		vref(object->handle);
		/* XXX what if the vnode is being destroyed? */
	}
}

/*
 * Object OBJ_CHAINLOCK lock handling.
 *
 * The caller can chain-lock backing objects recursively and then
 * use vm_object_chain_release_all() to undo the whole chain.
 *
 * Chain locks are used to prevent collapses and are only applicable
 * to OBJT_DEFAULT and OBJT_SWAP objects.  Chain locking operations
 * on other object types are ignored.  This is also important because
 * it allows e.g. the vnode underlying a memory mapping to take concurrent
 * faults.
 *
 * The object must usually be held on entry, though intermediate
 * objects need not be held on release.
 */
void
vm_object_chain_wait(vm_object_t object)
{
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	while (object->flags & OBJ_CHAINLOCK) {
		vm_object_set_flag(object, OBJ_CHAINWANT);
		tsleep(object, 0, "objchain", 0);
	}
}

void
vm_object_chain_acquire(vm_object_t object)
{
	if (object->type == OBJT_DEFAULT || object->type == OBJT_SWAP) {
		vm_object_chain_wait(object);
		vm_object_set_flag(object, OBJ_CHAINLOCK);
	}
}

void
vm_object_chain_release(vm_object_t object)
{
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	if (object->type == OBJT_DEFAULT || object->type == OBJT_SWAP) {
		KKASSERT(object->flags & OBJ_CHAINLOCK);
		if (object->flags & OBJ_CHAINWANT) {
			vm_object_clear_flag(object,
					     OBJ_CHAINLOCK | OBJ_CHAINWANT);
			wakeup(object);
		} else {
			vm_object_clear_flag(object, OBJ_CHAINLOCK);
		}
	}
}

/*
 * This releases the entire chain of objects from first_object to and
 * including stopobj, flowing through object->backing_object.
 *
 * We release stopobj first as an optimization as this object is most
 * likely to be shared across multiple processes.
 */
void
vm_object_chain_release_all(vm_object_t first_object, vm_object_t stopobj)
{
	vm_object_t backing_object;
	vm_object_t object;

	vm_object_chain_release(stopobj);
	object = first_object;

	while (object != stopobj) {
		KKASSERT(object);
		if (object != first_object)
			vm_object_hold(object);
		backing_object = object->backing_object;
		vm_object_chain_release(object);
		if (object != first_object)
			vm_object_drop(object);
		object = backing_object;
	}
}

/*
 * Dereference an object and its underlying vnode.
 *
 * The object must be held exclusively and will remain held on return.
 * (We don't need an atomic op due to the exclusivity).
 */
static void
vm_object_vndeallocate(vm_object_t object)
{
	struct vnode *vp = (struct vnode *) object->handle;

	KASSERT(object->type == OBJT_VNODE,
	    ("vm_object_vndeallocate: not a vnode object"));
	KASSERT(vp != NULL, ("vm_object_vndeallocate: missing vp"));
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
#ifdef INVARIANTS
	if (object->ref_count == 0) {
		vprint("vm_object_vndeallocate", vp);
		panic("vm_object_vndeallocate: bad object reference count");
	}
#endif
	object->ref_count--;
	if (object->ref_count == 0)
		vclrflags(vp, VTEXT);
	vrele(vp);
}

/*
 * Release a reference to the specified object, gained either through a
 * vm_object_allocate or a vm_object_reference call.  When all references
 * are gone, storage associated with this object may be relinquished.
 *
 * The caller does not have to hold the object locked but must have control
 * over the reference in question in order to guarantee that the object
 * does not get ripped out from under us.
 *
 * XXX Currently all deallocations require an exclusive lock.
 */
void
vm_object_deallocate(vm_object_t object)
{
	if (object) {
		vm_object_hold(object);
		vm_object_deallocate_locked(object);
		vm_object_drop(object);
	}
}

void
vm_object_deallocate_locked(vm_object_t object)
{
	struct vm_object_dealloc_list *dlist = NULL;
	struct vm_object_dealloc_list *dtmp;
	vm_object_t temp;
	int must_drop = 0;

	/*
	 * We may chain deallocate object, but additional objects may
	 * collect on the dlist which also have to be deallocated.  We
	 * must avoid a recursion, vm_object chains can get deep.
	 */
again:
	while (object != NULL) {
		ASSERT_LWKT_TOKEN_HELD_EXCL(&object->token);
#if 0
		/*
		 * Don't rip a ref_count out from under an object undergoing
		 * collapse, it will confuse the collapse code.
		 */
		vm_object_chain_wait(object);
#endif
		if (object->type == OBJT_VNODE) {
			vm_object_vndeallocate(object);
			break;
		}

		if (object->ref_count == 0) {
			panic("vm_object_deallocate: object deallocated "
			      "too many times: %d", object->type);
		}
		if (object->ref_count > 2) {
			object->ref_count--;
			break;
		}

		/*
		 * Here on ref_count of one or two, which are special cases for
		 * objects.
		 *
		 * Nominal ref_count > 1 case if the second ref is not from
		 * a shadow.
		 *
		 * (ONEMAPPING only applies to DEFAULT AND SWAP objects)
		 */
		if (object->ref_count == 2 && object->shadow_count == 0) {
			if (object->type == OBJT_DEFAULT ||
			    object->type == OBJT_SWAP) {
				vm_object_set_flag(object, OBJ_ONEMAPPING);
			}
			object->ref_count--;
			break;
		}

		/*
		 * If the second ref is from a shadow we chain along it
		 * upwards if object's handle is exhausted.
		 *
		 * We have to decrement object->ref_count before potentially
		 * collapsing the first shadow object or the collapse code
		 * will not be able to handle the degenerate case to remove
		 * object.  However, if we do it too early the object can
		 * get ripped out from under us.
		 */
		if (object->ref_count == 2 && object->shadow_count == 1 &&
		    object->handle == NULL && (object->type == OBJT_DEFAULT ||
					       object->type == OBJT_SWAP)) {
			temp = LIST_FIRST(&object->shadow_head);
			KKASSERT(temp != NULL);
			vm_object_hold(temp);

			/*
			 * Wait for any paging to complete so the collapse
			 * doesn't (or isn't likely to) qcollapse.  pip
			 * waiting must occur before we acquire the
			 * chainlock.
			 */
			while (
				temp->paging_in_progress ||
				object->paging_in_progress
			) {
				vm_object_pip_wait(temp, "objde1");
				vm_object_pip_wait(object, "objde2");
			}

			/*
			 * If the parent is locked we have to give up, as
			 * otherwise we would be acquiring locks in the
			 * wrong order and potentially deadlock.
			 */
			if (temp->flags & OBJ_CHAINLOCK) {
				vm_object_drop(temp);
				goto skip;
			}
			vm_object_chain_acquire(temp);

			/*
			 * Recheck/retry after the hold and the paging
			 * wait, both of which can block us.
			 */
			if (object->ref_count != 2 ||
			    object->shadow_count != 1 ||
			    object->handle ||
			    LIST_FIRST(&object->shadow_head) != temp ||
			    (object->type != OBJT_DEFAULT &&
			     object->type != OBJT_SWAP)) {
				vm_object_chain_release(temp);
				vm_object_drop(temp);
				continue;
			}

			/*
			 * We can safely drop object's ref_count now.
			 */
			KKASSERT(object->ref_count == 2);
			object->ref_count--;

			/*
			 * If our single parent is not collapseable just
			 * decrement ref_count (2->1) and stop.
			 */
			if (temp->handle || (temp->type != OBJT_DEFAULT &&
					     temp->type != OBJT_SWAP)) {
				vm_object_chain_release(temp);
				vm_object_drop(temp);
				break;
			}

			/*
			 * At this point we have already dropped object's
			 * ref_count so it is possible for a race to
			 * deallocate obj out from under us.  Any collapse
			 * will re-check the situation.  We must not block
			 * until we are able to collapse.
			 *
			 * Bump temp's ref_count to avoid an unwanted
			 * degenerate recursion (can't call
			 * vm_object_reference_locked() because it asserts
			 * that CHAINLOCK is not set).
			 */
			temp->ref_count++;
			KKASSERT(temp->ref_count > 1);

			/*
			 * Collapse temp, then deallocate the extra ref
			 * formally.
			 */
			vm_object_collapse(temp, &dlist);
			vm_object_chain_release(temp);
			if (must_drop) {
				vm_object_lock_swap();
				vm_object_drop(object);
			}
			object = temp;
			must_drop = 1;
			continue;
		}

		/*
		 * Drop the ref and handle termination on the 1->0 transition.
		 * We may have blocked above so we have to recheck.
		 */
skip:
		KKASSERT(object->ref_count != 0);
		if (object->ref_count >= 2) {
			object->ref_count--;
			break;
		}
		KKASSERT(object->ref_count == 1);

		/*
		 * 1->0 transition.  Chain through the backing_object.
		 * Maintain the ref until we've located the backing object,
		 * then re-check.
		 */
		while ((temp = object->backing_object) != NULL) {
			vm_object_hold(temp);
			if (temp == object->backing_object)
				break;
			vm_object_drop(temp);
		}

		/*
		 * 1->0 transition verified, retry if ref_count is no longer
		 * 1.  Otherwise disconnect the backing_object (temp) and
		 * clean up.
		 */
		if (object->ref_count != 1) {
			vm_object_drop(temp);
			continue;
		}

		/*
		 * It shouldn't be possible for the object to be chain locked
		 * if we're removing the last ref on it.
		 */
		KKASSERT((object->flags & OBJ_CHAINLOCK) == 0);

		if (temp) {
			LIST_REMOVE(object, shadow_list);
			temp->shadow_count--;
			temp->generation++;
			object->backing_object = NULL;
		}

		--object->ref_count;
		if ((object->flags & OBJ_DEAD) == 0)
			vm_object_terminate(object);
		if (must_drop && temp)
			vm_object_lock_swap();
		if (must_drop)
			vm_object_drop(object);
		object = temp;
		must_drop = 1;
	}
	if (must_drop && object)
		vm_object_drop(object);

	/*
	 * Additional tail recursion on dlist.  Avoid a recursion.  Objects
	 * on the dlist have a hold count but are not locked.
	 */
	if ((dtmp = dlist) != NULL) {
		dlist = dtmp->next;
		object = dtmp->object;
		kfree(dtmp, M_TEMP);

		vm_object_lock(object);	/* already held, add lock */
		must_drop = 1;		/* and we're responsible for it */
		goto again;
	}
}

/*
 * Destroy the specified object, freeing up related resources.
 *
 * The object must have zero references.
 *
 * The object must held.  The caller is responsible for dropping the object
 * after terminate returns.  Terminate does NOT drop the object.
 */
static int vm_object_terminate_callback(vm_page_t p, void *data);

void
vm_object_terminate(vm_object_t object)
{
	/*
	 * Make sure no one uses us.  Once we set OBJ_DEAD we should be
	 * able to safely block.
	 */
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	KKASSERT((object->flags & OBJ_DEAD) == 0);
	vm_object_set_flag(object, OBJ_DEAD);

	/*
	 * Wait for the pageout daemon to be done with the object
	 */
	vm_object_pip_wait(object, "objtrm1");

	KASSERT(!object->paging_in_progress,
		("vm_object_terminate: pageout in progress"));

	/*
	 * Clean and free the pages, as appropriate. All references to the
	 * object are gone, so we don't need to lock it.
	 */
	if (object->type == OBJT_VNODE) {
		struct vnode *vp;

		/*
		 * Clean pages and flush buffers.
		 *
		 * NOTE!  TMPFS buffer flushes do not typically flush the
		 *	  actual page to swap as this would be highly
		 *	  inefficient, and normal filesystems usually wrap
		 *	  page flushes with buffer cache buffers.
		 *
		 *	  To deal with this we have to call vinvalbuf() both
		 *	  before and after the vm_object_page_clean().
		 */
		vp = (struct vnode *) object->handle;
		vinvalbuf(vp, V_SAVE, 0, 0);
		vm_object_page_clean(object, 0, 0, OBJPC_SYNC);
		vinvalbuf(vp, V_SAVE, 0, 0);
	}

	/*
	 * Wait for any I/O to complete, after which there had better not
	 * be any references left on the object.
	 */
	vm_object_pip_wait(object, "objtrm2");

	if (object->ref_count != 0) {
		panic("vm_object_terminate: object with references, "
		      "ref_count=%d", object->ref_count);
	}

	/*
	 * Cleanup any shared pmaps associated with this object.
	 */
	pmap_object_free(object);

	/*
	 * Now free any remaining pages. For internal objects, this also
	 * removes them from paging queues. Don't free wired pages, just
	 * remove them from the object. 
	 */
	vm_page_rb_tree_RB_SCAN(&object->rb_memq, NULL,
				vm_object_terminate_callback, NULL);

	/*
	 * Let the pager know object is dead.
	 */
	vm_pager_deallocate(object);

	/*
	 * Wait for the object hold count to hit 1, clean out pages as
	 * we go.  vmobj_token interlocks any race conditions that might
	 * pick the object up from the vm_object_list after we have cleared
	 * rb_memq.
	 */
	for (;;) {
		if (RB_ROOT(&object->rb_memq) == NULL)
			break;
		kprintf("vm_object_terminate: Warning, object %p "
			"still has %d pages\n",
			object, object->resident_page_count);
		vm_page_rb_tree_RB_SCAN(&object->rb_memq, NULL,
					vm_object_terminate_callback, NULL);
	}

	/*
	 * There had better not be any pages left
	 */
	KKASSERT(object->resident_page_count == 0);

	/*
	 * Remove the object from the global object list.
	 */
	lwkt_gettoken(&vmobj_token);
	TAILQ_REMOVE(&vm_object_list, object, object_list);
	vm_object_count--;
	lwkt_reltoken(&vmobj_token);
	vm_object_dead_wakeup(object);

	if (object->ref_count != 0) {
		panic("vm_object_terminate2: object with references, "
		      "ref_count=%d", object->ref_count);
	}

	/*
	 * NOTE: The object hold_count is at least 1, so we cannot zfree()
	 *	 the object here.  See vm_object_drop().
	 */
}

/*
 * The caller must hold the object.
 */
static int
vm_object_terminate_callback(vm_page_t p, void *data __unused)
{
	vm_object_t object;

	object = p->object;
	vm_page_busy_wait(p, TRUE, "vmpgtrm");
	if (object != p->object) {
		kprintf("vm_object_terminate: Warning: Encountered "
			"busied page %p on queue %d\n", p, p->queue);
		vm_page_wakeup(p);
	} else if (p->wire_count == 0) {
		/*
		 * NOTE: p->dirty and PG_NEED_COMMIT are ignored.
		 */
		vm_page_free(p);
		mycpu->gd_cnt.v_pfree++;
	} else {
		if (p->queue != PQ_NONE)
			kprintf("vm_object_terminate: Warning: Encountered "
				"wired page %p on queue %d\n", p, p->queue);
		vm_page_remove(p);
		vm_page_wakeup(p);
	}
	lwkt_yield();
	return(0);
}

/*
 * The object is dead but still has an object<->pager association.  Sleep
 * and return.  The caller typically retests the association in a loop.
 *
 * The caller must hold the object.
 */
void
vm_object_dead_sleep(vm_object_t object, const char *wmesg)
{
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	if (object->handle) {
		vm_object_set_flag(object, OBJ_DEADWNT);
		tsleep(object, 0, wmesg, 0);
		/* object may be invalid after this point */
	}
}

/*
 * Wakeup anyone waiting for the object<->pager disassociation on
 * a dead object.
 *
 * The caller must hold the object.
 */
void
vm_object_dead_wakeup(vm_object_t object)
{
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	if (object->flags & OBJ_DEADWNT) {
		vm_object_clear_flag(object, OBJ_DEADWNT);
		wakeup(object);
	}
}

/*
 * Clean all dirty pages in the specified range of object.  Leaves page
 * on whatever queue it is currently on.   If NOSYNC is set then do not
 * write out pages with PG_NOSYNC set (originally comes from MAP_NOSYNC),
 * leaving the object dirty.
 *
 * When stuffing pages asynchronously, allow clustering.  XXX we need a
 * synchronous clustering mode implementation.
 *
 * Odd semantics: if start == end, we clean everything.
 *
 * The object must be locked? XXX
 */
static int vm_object_page_clean_pass1(struct vm_page *p, void *data);
static int vm_object_page_clean_pass2(struct vm_page *p, void *data);

void
vm_object_page_clean(vm_object_t object, vm_pindex_t start, vm_pindex_t end,
		     int flags)
{
	struct rb_vm_page_scan_info info;
	struct vnode *vp;
	int wholescan;
	int pagerflags;
	int generation;

	vm_object_hold(object);
	if (object->type != OBJT_VNODE ||
	    (object->flags & OBJ_MIGHTBEDIRTY) == 0) {
		vm_object_drop(object);
		return;
	}

	pagerflags = (flags & (OBJPC_SYNC | OBJPC_INVAL)) ? 
			VM_PAGER_PUT_SYNC : VM_PAGER_CLUSTER_OK;
	pagerflags |= (flags & OBJPC_INVAL) ? VM_PAGER_PUT_INVAL : 0;

	vp = object->handle;

	/*
	 * Interlock other major object operations.  This allows us to 
	 * temporarily clear OBJ_WRITEABLE and OBJ_MIGHTBEDIRTY.
	 */
	vm_object_set_flag(object, OBJ_CLEANING);

	/*
	 * Handle 'entire object' case
	 */
	info.start_pindex = start;
	if (end == 0) {
		info.end_pindex = object->size - 1;
	} else {
		info.end_pindex = end - 1;
	}
	wholescan = (start == 0 && info.end_pindex == object->size - 1);
	info.limit = flags;
	info.pagerflags = pagerflags;
	info.object = object;

	/*
	 * If cleaning the entire object do a pass to mark the pages read-only.
	 * If everything worked out ok, clear OBJ_WRITEABLE and
	 * OBJ_MIGHTBEDIRTY.
	 */
	if (wholescan) {
		info.error = 0;
		vm_page_rb_tree_RB_SCAN(&object->rb_memq, rb_vm_page_scancmp,
					vm_object_page_clean_pass1, &info);
		if (info.error == 0) {
			vm_object_clear_flag(object,
					     OBJ_WRITEABLE|OBJ_MIGHTBEDIRTY);
			if (object->type == OBJT_VNODE &&
			    (vp = (struct vnode *)object->handle) != NULL) {
				if (vp->v_flag & VOBJDIRTY) 
					vclrflags(vp, VOBJDIRTY);
			}
		}
	}

	/*
	 * Do a pass to clean all the dirty pages we find.
	 */
	do {
		info.error = 0;
		generation = object->generation;
		vm_page_rb_tree_RB_SCAN(&object->rb_memq, rb_vm_page_scancmp,
					vm_object_page_clean_pass2, &info);
	} while (info.error || generation != object->generation);

	vm_object_clear_flag(object, OBJ_CLEANING);
	vm_object_drop(object);
}

/*
 * The caller must hold the object.
 */
static 
int
vm_object_page_clean_pass1(struct vm_page *p, void *data)
{
	struct rb_vm_page_scan_info *info = data;

	vm_page_flag_set(p, PG_CLEANCHK);
	if ((info->limit & OBJPC_NOSYNC) && (p->flags & PG_NOSYNC)) {
		info->error = 1;
	} else if (vm_page_busy_try(p, FALSE) == 0) {
		vm_page_protect(p, VM_PROT_READ);	/* must not block */
		vm_page_wakeup(p);
	} else {
		info->error = 1;
	}
	lwkt_yield();
	return(0);
}

/*
 * The caller must hold the object
 */
static 
int
vm_object_page_clean_pass2(struct vm_page *p, void *data)
{
	struct rb_vm_page_scan_info *info = data;
	int generation;

	/*
	 * Do not mess with pages that were inserted after we started
	 * the cleaning pass.
	 */
	if ((p->flags & PG_CLEANCHK) == 0)
		goto done;

	generation = info->object->generation;
	vm_page_busy_wait(p, TRUE, "vpcwai");
	if (p->object != info->object ||
	    info->object->generation != generation) {
		info->error = 1;
		vm_page_wakeup(p);
		goto done;
	}

	/*
	 * Before wasting time traversing the pmaps, check for trivial
	 * cases where the page cannot be dirty.
	 */
	if (p->valid == 0 || (p->queue - p->pc) == PQ_CACHE) {
		KKASSERT((p->dirty & p->valid) == 0 &&
			 (p->flags & PG_NEED_COMMIT) == 0);
		vm_page_wakeup(p);
		goto done;
	}

	/*
	 * Check whether the page is dirty or not.  The page has been set
	 * to be read-only so the check will not race a user dirtying the
	 * page.
	 */
	vm_page_test_dirty(p);
	if ((p->dirty & p->valid) == 0 && (p->flags & PG_NEED_COMMIT) == 0) {
		vm_page_flag_clear(p, PG_CLEANCHK);
		vm_page_wakeup(p);
		goto done;
	}

	/*
	 * If we have been asked to skip nosync pages and this is a
	 * nosync page, skip it.  Note that the object flags were
	 * not cleared in this case (because pass1 will have returned an
	 * error), so we do not have to set them.
	 */
	if ((info->limit & OBJPC_NOSYNC) && (p->flags & PG_NOSYNC)) {
		vm_page_flag_clear(p, PG_CLEANCHK);
		vm_page_wakeup(p);
		goto done;
	}

	/*
	 * Flush as many pages as we can.  PG_CLEANCHK will be cleared on
	 * the pages that get successfully flushed.  Set info->error if
	 * we raced an object modification.
	 */
	vm_object_page_collect_flush(info->object, p, info->pagerflags);
	vm_wait_nominal();
done:
	lwkt_yield();
	return(0);
}

/*
 * Collect the specified page and nearby pages and flush them out.
 * The number of pages flushed is returned.  The passed page is busied
 * by the caller and we are responsible for its disposition.
 *
 * The caller must hold the object.
 */
static void
vm_object_page_collect_flush(vm_object_t object, vm_page_t p, int pagerflags)
{
	int error;
	int is;
	int ib;
	int i;
	int page_base;
	vm_pindex_t pi;
	vm_page_t ma[BLIST_MAX_ALLOC];

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));

	pi = p->pindex;
	page_base = pi % BLIST_MAX_ALLOC;
	ma[page_base] = p;
	ib = page_base - 1;
	is = page_base + 1;

	while (ib >= 0) {
		vm_page_t tp;

		tp = vm_page_lookup_busy_try(object, pi - page_base + ib,
					     TRUE, &error);
		if (error)
			break;
		if (tp == NULL)
			break;
		if ((pagerflags & VM_PAGER_IGNORE_CLEANCHK) == 0 &&
		    (tp->flags & PG_CLEANCHK) == 0) {
			vm_page_wakeup(tp);
			break;
		}
		if ((tp->queue - tp->pc) == PQ_CACHE) {
			vm_page_flag_clear(tp, PG_CLEANCHK);
			vm_page_wakeup(tp);
			break;
		}
		vm_page_test_dirty(tp);
		if ((tp->dirty & tp->valid) == 0 &&
		    (tp->flags & PG_NEED_COMMIT) == 0) {
			vm_page_flag_clear(tp, PG_CLEANCHK);
			vm_page_wakeup(tp);
			break;
		}
		ma[ib] = tp;
		--ib;
	}
	++ib;	/* fixup */

	while (is < BLIST_MAX_ALLOC &&
	       pi - page_base + is < object->size) {
		vm_page_t tp;

		tp = vm_page_lookup_busy_try(object, pi - page_base + is,
					     TRUE, &error);
		if (error)
			break;
		if (tp == NULL)
			break;
		if ((pagerflags & VM_PAGER_IGNORE_CLEANCHK) == 0 &&
		    (tp->flags & PG_CLEANCHK) == 0) {
			vm_page_wakeup(tp);
			break;
		}
		if ((tp->queue - tp->pc) == PQ_CACHE) {
			vm_page_flag_clear(tp, PG_CLEANCHK);
			vm_page_wakeup(tp);
			break;
		}
		vm_page_test_dirty(tp);
		if ((tp->dirty & tp->valid) == 0 &&
		    (tp->flags & PG_NEED_COMMIT) == 0) {
			vm_page_flag_clear(tp, PG_CLEANCHK);
			vm_page_wakeup(tp);
			break;
		}
		ma[is] = tp;
		++is;
	}

	/*
	 * All pages in the ma[] array are busied now
	 */
	for (i = ib; i < is; ++i) {
		vm_page_flag_clear(ma[i], PG_CLEANCHK);
		vm_page_hold(ma[i]);	/* XXX need this any more? */
	}
	vm_pageout_flush(&ma[ib], is - ib, pagerflags);
	for (i = ib; i < is; ++i)	/* XXX need this any more? */
		vm_page_unhold(ma[i]);
}

/*
 * Same as vm_object_pmap_copy, except range checking really
 * works, and is meant for small sections of an object.
 *
 * This code protects resident pages by making them read-only
 * and is typically called on a fork or split when a page
 * is converted to copy-on-write.  
 *
 * NOTE: If the page is already at VM_PROT_NONE, calling
 * vm_page_protect will have no effect.
 */
void
vm_object_pmap_copy_1(vm_object_t object, vm_pindex_t start, vm_pindex_t end)
{
	vm_pindex_t idx;
	vm_page_t p;

	if (object == NULL || (object->flags & OBJ_WRITEABLE) == 0)
		return;

	vm_object_hold(object);
	for (idx = start; idx < end; idx++) {
		p = vm_page_lookup(object, idx);
		if (p == NULL)
			continue;
		vm_page_protect(p, VM_PROT_READ);
	}
	vm_object_drop(object);
}

/*
 * Removes all physical pages in the specified object range from all
 * physical maps.
 *
 * The object must *not* be locked.
 */

static int vm_object_pmap_remove_callback(vm_page_t p, void *data);

void
vm_object_pmap_remove(vm_object_t object, vm_pindex_t start, vm_pindex_t end)
{
	struct rb_vm_page_scan_info info;

	if (object == NULL)
		return;
	info.start_pindex = start;
	info.end_pindex = end - 1;

	vm_object_hold(object);
	vm_page_rb_tree_RB_SCAN(&object->rb_memq, rb_vm_page_scancmp,
				vm_object_pmap_remove_callback, &info);
	if (start == 0 && end == object->size)
		vm_object_clear_flag(object, OBJ_WRITEABLE);
	vm_object_drop(object);
}

/*
 * The caller must hold the object
 */
static int
vm_object_pmap_remove_callback(vm_page_t p, void *data __unused)
{
	vm_page_protect(p, VM_PROT_NONE);
	return(0);
}

/*
 * Implements the madvise function at the object/page level.
 *
 * MADV_WILLNEED	(any object)
 *
 *	Activate the specified pages if they are resident.
 *
 * MADV_DONTNEED	(any object)
 *
 *	Deactivate the specified pages if they are resident.
 *
 * MADV_FREE	(OBJT_DEFAULT/OBJT_SWAP objects, OBJ_ONEMAPPING only)
 *
 *	Deactivate and clean the specified pages if they are
 *	resident.  This permits the process to reuse the pages
 *	without faulting or the kernel to reclaim the pages
 *	without I/O.
 *
 * No requirements.
 */
void
vm_object_madvise(vm_object_t object, vm_pindex_t pindex, int count, int advise)
{
	vm_pindex_t end, tpindex;
	vm_object_t tobject;
	vm_object_t xobj;
	vm_page_t m;
	int error;

	if (object == NULL)
		return;

	end = pindex + count;

	vm_object_hold(object);
	tobject = object;

	/*
	 * Locate and adjust resident pages
	 */
	for (; pindex < end; pindex += 1) {
relookup:
		if (tobject != object)
			vm_object_drop(tobject);
		tobject = object;
		tpindex = pindex;
shadowlookup:
		/*
		 * MADV_FREE only operates on OBJT_DEFAULT or OBJT_SWAP pages
		 * and those pages must be OBJ_ONEMAPPING.
		 */
		if (advise == MADV_FREE) {
			if ((tobject->type != OBJT_DEFAULT &&
			     tobject->type != OBJT_SWAP) ||
			    (tobject->flags & OBJ_ONEMAPPING) == 0) {
				continue;
			}
		}

		m = vm_page_lookup_busy_try(tobject, tpindex, TRUE, &error);

		if (error) {
			vm_page_sleep_busy(m, TRUE, "madvpo");
			goto relookup;
		}
		if (m == NULL) {
			/*
			 * There may be swap even if there is no backing page
			 */
			if (advise == MADV_FREE && tobject->type == OBJT_SWAP)
				swap_pager_freespace(tobject, tpindex, 1);

			/*
			 * next object
			 */
			while ((xobj = tobject->backing_object) != NULL) {
				KKASSERT(xobj != object);
				vm_object_hold(xobj);
				if (xobj == tobject->backing_object)
					break;
				vm_object_drop(xobj);
			}
			if (xobj == NULL)
				continue;
			tpindex += OFF_TO_IDX(tobject->backing_object_offset);
			if (tobject != object) {
				vm_object_lock_swap();
				vm_object_drop(tobject);
			}
			tobject = xobj;
			goto shadowlookup;
		}

		/*
		 * If the page is not in a normal active state, we skip it.
		 * If the page is not managed there are no page queues to
		 * mess with.  Things can break if we mess with pages in
		 * any of the below states.
		 */
		if (m->wire_count ||
		    (m->flags & (PG_UNMANAGED | PG_NEED_COMMIT)) ||
		    m->valid != VM_PAGE_BITS_ALL
		) {
			vm_page_wakeup(m);
			continue;
		}

		/*
		 * Theoretically once a page is known not to be busy, an
		 * interrupt cannot come along and rip it out from under us.
		 */

		if (advise == MADV_WILLNEED) {
			vm_page_activate(m);
		} else if (advise == MADV_DONTNEED) {
			vm_page_dontneed(m);
		} else if (advise == MADV_FREE) {
			/*
			 * Mark the page clean.  This will allow the page
			 * to be freed up by the system.  However, such pages
			 * are often reused quickly by malloc()/free()
			 * so we do not do anything that would cause
			 * a page fault if we can help it.
			 *
			 * Specifically, we do not try to actually free
			 * the page now nor do we try to put it in the
			 * cache (which would cause a page fault on reuse).
			 *
			 * But we do make the page is freeable as we
			 * can without actually taking the step of unmapping
			 * it.
			 */
			pmap_clear_modify(m);
			m->dirty = 0;
			m->act_count = 0;
			vm_page_dontneed(m);
			if (tobject->type == OBJT_SWAP)
				swap_pager_freespace(tobject, tpindex, 1);
		}
		vm_page_wakeup(m);
	}	
	if (tobject != object)
		vm_object_drop(tobject);
	vm_object_drop(object);
}

/*
 * Create a new object which is backed by the specified existing object
 * range.  Replace the pointer and offset that was pointing at the existing
 * object with the pointer/offset for the new object.
 *
 * No other requirements.
 */
void
vm_object_shadow(vm_object_t *objectp, vm_ooffset_t *offset, vm_size_t length,
		 int addref)
{
	vm_object_t source;
	vm_object_t result;

	source = *objectp;

	/*
	 * Don't create the new object if the old object isn't shared.
	 * We have to chain wait before adding the reference to avoid
	 * racing a collapse or deallocation.
	 *
	 * Add the additional ref to source here to avoid racing a later
	 * collapse or deallocation. Clear the ONEMAPPING flag whether
	 * addref is TRUE or not in this case because the original object
	 * will be shadowed.
	 */
	if (source) {
		vm_object_hold(source);
		vm_object_chain_wait(source);
		if (source->ref_count == 1 &&
		    source->handle == NULL &&
		    (source->type == OBJT_DEFAULT ||
		     source->type == OBJT_SWAP)) {
			vm_object_drop(source);
			if (addref) {
				vm_object_reference_locked(source);
				vm_object_clear_flag(source, OBJ_ONEMAPPING);
			}
			return;
		}
		vm_object_reference_locked(source);
		vm_object_clear_flag(source, OBJ_ONEMAPPING);
	}

	/*
	 * Allocate a new object with the given length.  The new object
	 * is returned referenced but we may have to add another one.
	 * If we are adding a second reference we must clear OBJ_ONEMAPPING.
	 * (typically because the caller is about to clone a vm_map_entry).
	 *
	 * The source object currently has an extra reference to prevent
	 * collapses into it while we mess with its shadow list, which
	 * we will remove later in this routine.
	 */
	if ((result = vm_object_allocate(OBJT_DEFAULT, length)) == NULL)
		panic("vm_object_shadow: no object for shadowing");
	vm_object_hold(result);
	if (addref) {
		vm_object_reference_locked(result);
		vm_object_clear_flag(result, OBJ_ONEMAPPING);
	}

	/*
	 * The new object shadows the source object.  Chain wait before
	 * adjusting shadow_count or the shadow list to avoid races.
	 *
	 * Try to optimize the result object's page color when shadowing
	 * in order to maintain page coloring consistency in the combined 
	 * shadowed object.
	 */
	KKASSERT(result->backing_object == NULL);
	result->backing_object = source;
	if (source) {
		vm_object_chain_wait(source);
		LIST_INSERT_HEAD(&source->shadow_head, result, shadow_list);
		source->shadow_count++;
		source->generation++;
		/* cpu localization twist */
		result->pg_color = (int)(intptr_t)curthread;
	}

	/*
	 * Adjust the return storage.  Drop the ref on source before
	 * returning.
	 */
	result->backing_object_offset = *offset;
	vm_object_drop(result);
	*offset = 0;
	if (source) {
		vm_object_deallocate_locked(source);
		vm_object_drop(source);
	}

	/*
	 * Return the new things
	 */
	*objectp = result;
}

#define	OBSC_TEST_ALL_SHADOWED	0x0001
#define	OBSC_COLLAPSE_NOWAIT	0x0002
#define	OBSC_COLLAPSE_WAIT	0x0004

static int vm_object_backing_scan_callback(vm_page_t p, void *data);

/*
 * The caller must hold the object.
 */
static __inline int
vm_object_backing_scan(vm_object_t object, vm_object_t backing_object, int op)
{
	struct rb_vm_page_scan_info info;

	vm_object_assert_held(object);
	vm_object_assert_held(backing_object);

	KKASSERT(backing_object == object->backing_object);
	info.backing_offset_index = OFF_TO_IDX(object->backing_object_offset);

	/*
	 * Initial conditions
	 */
	if (op & OBSC_TEST_ALL_SHADOWED) {
		/*
		 * We do not want to have to test for the existence of
		 * swap pages in the backing object.  XXX but with the
		 * new swapper this would be pretty easy to do.
		 *
		 * XXX what about anonymous MAP_SHARED memory that hasn't
		 * been ZFOD faulted yet?  If we do not test for this, the
		 * shadow test may succeed! XXX
		 */
		if (backing_object->type != OBJT_DEFAULT)
			return(0);
	}
	if (op & OBSC_COLLAPSE_WAIT) {
		KKASSERT((backing_object->flags & OBJ_DEAD) == 0);
		vm_object_set_flag(backing_object, OBJ_DEAD);
		lwkt_gettoken(&vmobj_token);
		TAILQ_REMOVE(&vm_object_list, backing_object, object_list);
		vm_object_count--;
		lwkt_reltoken(&vmobj_token);
		vm_object_dead_wakeup(backing_object);
	}

	/*
	 * Our scan.   We have to retry if a negative error code is returned,
	 * otherwise 0 or 1 will be returned in info.error.  0 Indicates that
	 * the scan had to be stopped because the parent does not completely
	 * shadow the child.
	 */
	info.object = object;
	info.backing_object = backing_object;
	info.limit = op;
	do {
		info.error = 1;
		vm_page_rb_tree_RB_SCAN(&backing_object->rb_memq, NULL,
					vm_object_backing_scan_callback,
					&info);
	} while (info.error < 0);

	return(info.error);
}

/*
 * The caller must hold the object.
 */
static int
vm_object_backing_scan_callback(vm_page_t p, void *data)
{
	struct rb_vm_page_scan_info *info = data;
	vm_object_t backing_object;
	vm_object_t object;
	vm_pindex_t pindex;
	vm_pindex_t new_pindex;
	vm_pindex_t backing_offset_index;
	int op;

	pindex = p->pindex;
	new_pindex = pindex - info->backing_offset_index;
	op = info->limit;
	object = info->object;
	backing_object = info->backing_object;
	backing_offset_index = info->backing_offset_index;

	if (op & OBSC_TEST_ALL_SHADOWED) {
		vm_page_t pp;

		/*
		 * Ignore pages outside the parent object's range
		 * and outside the parent object's mapping of the 
		 * backing object.
		 *
		 * note that we do not busy the backing object's
		 * page.
		 */
		if (pindex < backing_offset_index ||
		    new_pindex >= object->size
		) {
			return(0);
		}

		/*
		 * See if the parent has the page or if the parent's
		 * object pager has the page.  If the parent has the
		 * page but the page is not valid, the parent's
		 * object pager must have the page.
		 *
		 * If this fails, the parent does not completely shadow
		 * the object and we might as well give up now.
		 */
		pp = vm_page_lookup(object, new_pindex);
		if ((pp == NULL || pp->valid == 0) &&
		    !vm_pager_has_page(object, new_pindex)
		) {
			info->error = 0;	/* problemo */
			return(-1);		/* stop the scan */
		}
	}

	/*
	 * Check for busy page.  Note that we may have lost (p) when we
	 * possibly blocked above.
	 */
	if (op & (OBSC_COLLAPSE_WAIT | OBSC_COLLAPSE_NOWAIT)) {
		vm_page_t pp;

		if (vm_page_busy_try(p, TRUE)) {
			if (op & OBSC_COLLAPSE_NOWAIT) {
				return(0);
			} else {
				/*
				 * If we slept, anything could have
				 * happened.   Ask that the scan be restarted.
				 *
				 * Since the object is marked dead, the
				 * backing offset should not have changed.  
				 */
				vm_page_sleep_busy(p, TRUE, "vmocol");
				info->error = -1;
				return(-1);
			}
		}

		/*
		 * If (p) is no longer valid restart the scan.
		 */
		if (p->object != backing_object || p->pindex != pindex) {
			kprintf("vm_object_backing_scan: Warning: page "
				"%p ripped out from under us\n", p);
			vm_page_wakeup(p);
			info->error = -1;
			return(-1);
		}

		if (op & OBSC_COLLAPSE_NOWAIT) {
			if (p->valid == 0 ||
			    p->wire_count ||
			    (p->flags & PG_NEED_COMMIT)) {
				vm_page_wakeup(p);
				return(0);
			}
		} else {
			/* XXX what if p->valid == 0 , hold_count, etc? */
		}

		KASSERT(
		    p->object == backing_object,
		    ("vm_object_qcollapse(): object mismatch")
		);

		/*
		 * Destroy any associated swap
		 */
		if (backing_object->type == OBJT_SWAP)
			swap_pager_freespace(backing_object, p->pindex, 1);

		if (
		    p->pindex < backing_offset_index ||
		    new_pindex >= object->size
		) {
			/*
			 * Page is out of the parent object's range, we 
			 * can simply destroy it. 
			 */
			vm_page_protect(p, VM_PROT_NONE);
			vm_page_free(p);
			return(0);
		}

		pp = vm_page_lookup(object, new_pindex);
		if (pp != NULL || vm_pager_has_page(object, new_pindex)) {
			/*
			 * page already exists in parent OR swap exists
			 * for this location in the parent.  Destroy 
			 * the original page from the backing object.
			 *
			 * Leave the parent's page alone
			 */
			vm_page_protect(p, VM_PROT_NONE);
			vm_page_free(p);
			return(0);
		}

		/*
		 * Page does not exist in parent, rename the
		 * page from the backing object to the main object. 
		 *
		 * If the page was mapped to a process, it can remain 
		 * mapped through the rename.
		 */
		if ((p->queue - p->pc) == PQ_CACHE)
			vm_page_deactivate(p);

		vm_page_rename(p, object, new_pindex);
		vm_page_wakeup(p);
		/* page automatically made dirty by rename */
	}
	return(0);
}

/*
 * This version of collapse allows the operation to occur earlier and
 * when paging_in_progress is true for an object...  This is not a complete
 * operation, but should plug 99.9% of the rest of the leaks.
 *
 * The caller must hold the object and backing_object and both must be
 * chainlocked.
 *
 * (only called from vm_object_collapse)
 */
static void
vm_object_qcollapse(vm_object_t object, vm_object_t backing_object)
{
	if (backing_object->ref_count == 1) {
		backing_object->ref_count += 2;
		vm_object_backing_scan(object, backing_object,
				       OBSC_COLLAPSE_NOWAIT);
		backing_object->ref_count -= 2;
	}
}

/*
 * Collapse an object with the object backing it.  Pages in the backing
 * object are moved into the parent, and the backing object is deallocated.
 * Any conflict is resolved in favor of the parent's existing pages.
 *
 * object must be held and chain-locked on call.
 *
 * The caller must have an extra ref on object to prevent a race from
 * destroying it during the collapse.
 */
void
vm_object_collapse(vm_object_t object, struct vm_object_dealloc_list **dlistp)
{
	struct vm_object_dealloc_list *dlist = NULL;
	vm_object_t backing_object;

	/*
	 * Only one thread is attempting a collapse at any given moment.
	 * There are few restrictions for (object) that callers of this
	 * function check so reentrancy is likely.
	 */
	KKASSERT(object != NULL);
	vm_object_assert_held(object);
	KKASSERT(object->flags & OBJ_CHAINLOCK);

	for (;;) {
		vm_object_t bbobj;
		int dodealloc;

		/*
		 * We have to hold the backing object, check races.
		 */
		while ((backing_object = object->backing_object) != NULL) {
			vm_object_hold(backing_object);
			if (backing_object == object->backing_object)
				break;
			vm_object_drop(backing_object);
		}

		/*
		 * No backing object?  Nothing to collapse then.
		 */
		if (backing_object == NULL)
			break;

		/*
		 * You can't collapse with a non-default/non-swap object.
		 */
		if (backing_object->type != OBJT_DEFAULT &&
		    backing_object->type != OBJT_SWAP) {
			vm_object_drop(backing_object);
			backing_object = NULL;
			break;
		}

		/*
		 * Chain-lock the backing object too because if we
		 * successfully merge its pages into the top object we
		 * will collapse backing_object->backing_object as the
		 * new backing_object.  Re-check that it is still our
		 * backing object.
		 */
		vm_object_chain_acquire(backing_object);
		if (backing_object != object->backing_object) {
			vm_object_chain_release(backing_object);
			vm_object_drop(backing_object);
			continue;
		}

		/*
		 * we check the backing object first, because it is most likely
		 * not collapsable.
		 */
		if (backing_object->handle != NULL ||
		    (backing_object->type != OBJT_DEFAULT &&
		     backing_object->type != OBJT_SWAP) ||
		    (backing_object->flags & OBJ_DEAD) ||
		    object->handle != NULL ||
		    (object->type != OBJT_DEFAULT &&
		     object->type != OBJT_SWAP) ||
		    (object->flags & OBJ_DEAD)) {
			break;
		}

		/*
		 * If paging is in progress we can't do a normal collapse.
		 */
		if (
		    object->paging_in_progress != 0 ||
		    backing_object->paging_in_progress != 0
		) {
			vm_object_qcollapse(object, backing_object);
			break;
		}

		/*
		 * We know that we can either collapse the backing object (if
		 * the parent is the only reference to it) or (perhaps) have
		 * the parent bypass the object if the parent happens to shadow
		 * all the resident pages in the entire backing object.
		 *
		 * This is ignoring pager-backed pages such as swap pages.
		 * vm_object_backing_scan fails the shadowing test in this
		 * case.
		 */
		if (backing_object->ref_count == 1) {
			/*
			 * If there is exactly one reference to the backing
			 * object, we can collapse it into the parent.  
			 */
			KKASSERT(object->backing_object == backing_object);
			vm_object_backing_scan(object, backing_object,
					       OBSC_COLLAPSE_WAIT);

			/*
			 * Move the pager from backing_object to object.
			 */
			if (backing_object->type == OBJT_SWAP) {
				vm_object_pip_add(backing_object, 1);

				/*
				 * scrap the paging_offset junk and do a 
				 * discrete copy.  This also removes major 
				 * assumptions about how the swap-pager 
				 * works from where it doesn't belong.  The
				 * new swapper is able to optimize the
				 * destroy-source case.
				 */
				vm_object_pip_add(object, 1);
				swap_pager_copy(backing_object, object,
				    OFF_TO_IDX(object->backing_object_offset),
				    TRUE);
				vm_object_pip_wakeup(object);
				vm_object_pip_wakeup(backing_object);
			}

			/*
			 * Object now shadows whatever backing_object did.
			 * Remove object from backing_object's shadow_list.
			 */
			LIST_REMOVE(object, shadow_list);
			KKASSERT(object->backing_object == backing_object);
			backing_object->shadow_count--;
			backing_object->generation++;

			/*
			 * backing_object->backing_object moves from within
			 * backing_object to within object.
			 */
			while ((bbobj = backing_object->backing_object) != NULL) {
				vm_object_hold(bbobj);
				if (bbobj == backing_object->backing_object)
					break;
				vm_object_drop(bbobj);
			}
			if (bbobj) {
				LIST_REMOVE(backing_object, shadow_list);
				bbobj->shadow_count--;
				bbobj->generation++;
				backing_object->backing_object = NULL;
			}
			object->backing_object = bbobj;
			if (bbobj) {
				LIST_INSERT_HEAD(&bbobj->shadow_head,
						 object, shadow_list);
				bbobj->shadow_count++;
				bbobj->generation++;
			}

			object->backing_object_offset +=
				backing_object->backing_object_offset;

			vm_object_drop(bbobj);

			/*
			 * Discard the old backing_object.  Nothing should be
			 * able to ref it, other than a vm_map_split(),
			 * and vm_map_split() will stall on our chain lock.
			 * And we control the parent so it shouldn't be
			 * possible for it to go away either.
			 *
			 * Since the backing object has no pages, no pager
			 * left, and no object references within it, all
			 * that is necessary is to dispose of it.
			 */
			KASSERT(backing_object->ref_count == 1,
				("backing_object %p was somehow "
				 "re-referenced during collapse!",
				 backing_object));
			KASSERT(RB_EMPTY(&backing_object->rb_memq),
				("backing_object %p somehow has left "
				 "over pages during collapse!",
				 backing_object));

			/*
			 * The object can be destroyed.
			 *
			 * XXX just fall through and dodealloc instead
			 *     of forcing destruction?
			 */
			--backing_object->ref_count;
			if ((backing_object->flags & OBJ_DEAD) == 0)
				vm_object_terminate(backing_object);
			object_collapses++;
			dodealloc = 0;
		} else {
			/*
			 * If we do not entirely shadow the backing object,
			 * there is nothing we can do so we give up.
			 */
			if (vm_object_backing_scan(object, backing_object,
						OBSC_TEST_ALL_SHADOWED) == 0) {
				break;
			}

			/*
			 * bbobj is backing_object->backing_object.  Since
			 * object completely shadows backing_object we can
			 * bypass it and become backed by bbobj instead.
			 */
			while ((bbobj = backing_object->backing_object) != NULL) {
				vm_object_hold(bbobj);
				if (bbobj == backing_object->backing_object)
					break;
				vm_object_drop(bbobj);
			}

			/*
			 * Make object shadow bbobj instead of backing_object.
			 * Remove object from backing_object's shadow list.
			 *
			 * Deallocating backing_object will not remove
			 * it, since its reference count is at least 2.
			 */
			KKASSERT(object->backing_object == backing_object);
			LIST_REMOVE(object, shadow_list);
			backing_object->shadow_count--;
			backing_object->generation++;

			/*
			 * Add a ref to bbobj, bbobj now shadows object.
			 *
			 * NOTE: backing_object->backing_object still points
			 *	 to bbobj.  That relationship remains intact
			 *	 because backing_object has > 1 ref, so
			 *	 someone else is pointing to it (hence why
			 *	 we can't collapse it into object and can
			 *	 only handle the all-shadowed bypass case).
			 */
			if (bbobj) {
				vm_object_chain_wait(bbobj);
				vm_object_reference_locked(bbobj);
				LIST_INSERT_HEAD(&bbobj->shadow_head,
						 object, shadow_list);
				bbobj->shadow_count++;
				bbobj->generation++;
				object->backing_object_offset +=
					backing_object->backing_object_offset;
				object->backing_object = bbobj;
				vm_object_drop(bbobj);
			} else {
				object->backing_object = NULL;
			}

			/*
			 * Drop the reference count on backing_object.  To
			 * handle ref_count races properly we can't assume
			 * that the ref_count is still at least 2 so we
			 * have to actually call vm_object_deallocate()
			 * (after clearing the chainlock).
			 */
			object_bypasses++;
			dodealloc = 1;
		}

		/*
		 * Ok, we want to loop on the new object->bbobj association,
		 * possibly collapsing it further.  However if dodealloc is
		 * non-zero we have to deallocate the backing_object which
		 * itself can potentially undergo a collapse, creating a
		 * recursion depth issue with the LWKT token subsystem.
		 *
		 * In the case where we must deallocate the backing_object
		 * it is possible now that the backing_object has a single
		 * shadow count on some other object (not represented here
		 * as yet), since it no longer shadows us.  Thus when we
		 * call vm_object_deallocate() it may attempt to collapse
		 * itself into its remaining parent.
		 */
		if (dodealloc) {
			struct vm_object_dealloc_list *dtmp;

			vm_object_chain_release(backing_object);
			vm_object_unlock(backing_object);
			/* backing_object remains held */

			/*
			 * Auto-deallocation list for caller convenience.
			 */
			if (dlistp == NULL)
				dlistp = &dlist;

			dtmp = kmalloc(sizeof(*dtmp), M_TEMP, M_WAITOK);
			dtmp->object = backing_object;
			dtmp->next = *dlistp;
			*dlistp = dtmp;
		} else {
			vm_object_chain_release(backing_object);
			vm_object_drop(backing_object);
		}
		/* backing_object = NULL; not needed */
		/* loop */
	}

	/*
	 * Clean up any left over backing_object
	 */
	if (backing_object) {
		vm_object_chain_release(backing_object);
		vm_object_drop(backing_object);
	}

	/*
	 * Clean up any auto-deallocation list.  This is a convenience
	 * for top-level callers so they don't have to pass &dlist.
	 * Do not clean up any caller-passed dlistp, the caller will
	 * do that.
	 */
	if (dlist)
		vm_object_deallocate_list(&dlist);

}

/*
 * vm_object_collapse() may collect additional objects in need of
 * deallocation.  This routine deallocates these objects.  The
 * deallocation itself can trigger additional collapses (which the
 * deallocate function takes care of).  This procedure is used to
 * reduce procedural recursion since these vm_object shadow chains
 * can become quite long.
 */
void
vm_object_deallocate_list(struct vm_object_dealloc_list **dlistp)
{
	struct vm_object_dealloc_list *dlist;

	while ((dlist = *dlistp) != NULL) {
		*dlistp = dlist->next;
		vm_object_lock(dlist->object);
		vm_object_deallocate_locked(dlist->object);
		vm_object_drop(dlist->object);
		kfree(dlist, M_TEMP);
	}
}

/*
 * Removes all physical pages in the specified object range from the
 * object's list of pages.
 *
 * No requirements.
 */
static int vm_object_page_remove_callback(vm_page_t p, void *data);

void
vm_object_page_remove(vm_object_t object, vm_pindex_t start, vm_pindex_t end,
		      boolean_t clean_only)
{
	struct rb_vm_page_scan_info info;
	int all;

	/*
	 * Degenerate cases and assertions
	 */
	vm_object_hold(object);
	if (object == NULL ||
	    (object->resident_page_count == 0 && object->swblock_count == 0)) {
		vm_object_drop(object);
		return;
	}
	KASSERT(object->type != OBJT_PHYS, 
		("attempt to remove pages from a physical object"));

	/*
	 * Indicate that paging is occuring on the object
	 */
	vm_object_pip_add(object, 1);

	/*
	 * Figure out the actual removal range and whether we are removing
	 * the entire contents of the object or not.  If removing the entire
	 * contents, be sure to get all pages, even those that might be 
	 * beyond the end of the object.
	 */
	info.start_pindex = start;
	if (end == 0)
		info.end_pindex = (vm_pindex_t)-1;
	else
		info.end_pindex = end - 1;
	info.limit = clean_only;
	all = (start == 0 && info.end_pindex >= object->size - 1);

	/*
	 * Loop until we are sure we have gotten them all.
	 */
	do {
		info.error = 0;
		vm_page_rb_tree_RB_SCAN(&object->rb_memq, rb_vm_page_scancmp,
					vm_object_page_remove_callback, &info);
	} while (info.error);

	/*
	 * Remove any related swap if throwing away pages, or for
	 * non-swap objects (the swap is a clean copy in that case).
	 */
	if (object->type != OBJT_SWAP || clean_only == FALSE) {
		if (all)
			swap_pager_freespace_all(object);
		else
			swap_pager_freespace(object, info.start_pindex,
			     info.end_pindex - info.start_pindex + 1);
	}

	/*
	 * Cleanup
	 */
	vm_object_pip_wakeup(object);
	vm_object_drop(object);
}

/*
 * The caller must hold the object
 */
static int
vm_object_page_remove_callback(vm_page_t p, void *data)
{
	struct rb_vm_page_scan_info *info = data;

	if (vm_page_busy_try(p, TRUE)) {
		vm_page_sleep_busy(p, TRUE, "vmopar");
		info->error = 1;
		return(0);
	}

	/*
	 * Wired pages cannot be destroyed, but they can be invalidated
	 * and we do so if clean_only (limit) is not set.
	 *
	 * WARNING!  The page may be wired due to being part of a buffer
	 *	     cache buffer, and the buffer might be marked B_CACHE.
	 *	     This is fine as part of a truncation but VFSs must be
	 *	     sure to fix the buffer up when re-extending the file.
	 *
	 * NOTE!     PG_NEED_COMMIT is ignored.
	 */
	if (p->wire_count != 0) {
		vm_page_protect(p, VM_PROT_NONE);
		if (info->limit == 0)
			p->valid = 0;
		vm_page_wakeup(p);
		return(0);
	}

	/*
	 * limit is our clean_only flag.  If set and the page is dirty or
	 * requires a commit, do not free it.  If set and the page is being
	 * held by someone, do not free it.
	 */
	if (info->limit && p->valid) {
		vm_page_test_dirty(p);
		if ((p->valid & p->dirty) || (p->flags & PG_NEED_COMMIT)) {
			vm_page_wakeup(p);
			return(0);
		}
#if 0
		if (p->hold_count) {
			vm_page_wakeup(p);
			return(0);
		}
#endif
	}

	/*
	 * Destroy the page
	 */
	vm_page_protect(p, VM_PROT_NONE);
	vm_page_free(p);
	return(0);
}

/*
 * Coalesces two objects backing up adjoining regions of memory into a
 * single object.
 *
 * returns TRUE if objects were combined.
 *
 * NOTE: Only works at the moment if the second object is NULL -
 *	 if it's not, which object do we lock first?
 *
 * Parameters:
 *	prev_object	First object to coalesce
 *	prev_offset	Offset into prev_object
 *	next_object	Second object into coalesce
 *	next_offset	Offset into next_object
 *
 *	prev_size	Size of reference to prev_object
 *	next_size	Size of reference to next_object
 *
 * The caller does not need to hold (prev_object) but must have a stable
 * pointer to it (typically by holding the vm_map locked).
 */
boolean_t
vm_object_coalesce(vm_object_t prev_object, vm_pindex_t prev_pindex,
		   vm_size_t prev_size, vm_size_t next_size)
{
	vm_pindex_t next_pindex;

	if (prev_object == NULL)
		return (TRUE);

	vm_object_hold(prev_object);

	if (prev_object->type != OBJT_DEFAULT &&
	    prev_object->type != OBJT_SWAP) {
		vm_object_drop(prev_object);
		return (FALSE);
	}

	/*
	 * Try to collapse the object first
	 */
	vm_object_chain_acquire(prev_object);
	vm_object_collapse(prev_object, NULL);

	/*
	 * Can't coalesce if: . more than one reference . paged out . shadows
	 * another object . has a copy elsewhere (any of which mean that the
	 * pages not mapped to prev_entry may be in use anyway)
	 */

	if (prev_object->backing_object != NULL) {
		vm_object_chain_release(prev_object);
		vm_object_drop(prev_object);
		return (FALSE);
	}

	prev_size >>= PAGE_SHIFT;
	next_size >>= PAGE_SHIFT;
	next_pindex = prev_pindex + prev_size;

	if ((prev_object->ref_count > 1) &&
	    (prev_object->size != next_pindex)) {
		vm_object_chain_release(prev_object);
		vm_object_drop(prev_object);
		return (FALSE);
	}

	/*
	 * Remove any pages that may still be in the object from a previous
	 * deallocation.
	 */
	if (next_pindex < prev_object->size) {
		vm_object_page_remove(prev_object,
				      next_pindex,
				      next_pindex + next_size, FALSE);
		if (prev_object->type == OBJT_SWAP)
			swap_pager_freespace(prev_object,
					     next_pindex, next_size);
	}

	/*
	 * Extend the object if necessary.
	 */
	if (next_pindex + next_size > prev_object->size)
		prev_object->size = next_pindex + next_size;

	vm_object_chain_release(prev_object);
	vm_object_drop(prev_object);
	return (TRUE);
}

/*
 * Make the object writable and flag is being possibly dirty.
 *
 * The object might not be held, the related vnode is probably not
 * held either.  Object and vnode are stable by virtue of the vm_page
 * busied by the caller preventing destruction.
 *
 * If the related mount is flagged MNTK_THR_SYNC we need to call
 * vsetisdirty().  Filesystems using this option usually shortcut
 * synchronization by only scanning the syncer list.
 */
void
vm_object_set_writeable_dirty(vm_object_t object)
{
	struct vnode *vp;

	/*vm_object_assert_held(object);*/
	/*
	 * Avoid contention in vm fault path by checking the state before
	 * issuing an atomic op on it.
	 */
	if ((object->flags & (OBJ_WRITEABLE|OBJ_MIGHTBEDIRTY)) !=
	    (OBJ_WRITEABLE|OBJ_MIGHTBEDIRTY)) {
		vm_object_set_flag(object, OBJ_WRITEABLE|OBJ_MIGHTBEDIRTY);
	}
	if (object->type == OBJT_VNODE &&
	    (vp = (struct vnode *)object->handle) != NULL) {
		if ((vp->v_flag & VOBJDIRTY) == 0) {
			vsetflags(vp, VOBJDIRTY);
			if (vp->v_mount &&
			    (vp->v_mount->mnt_kern_flag & MNTK_THR_SYNC)) {
				vsetisdirty(vp);
			}
		}
	}
}

#include "opt_ddb.h"
#ifdef DDB
#include <sys/kernel.h>

#include <sys/cons.h>

#include <ddb/ddb.h>

static int	_vm_object_in_map (vm_map_t map, vm_object_t object,
				       vm_map_entry_t entry);
static int	vm_object_in_map (vm_object_t object);

/*
 * The caller must hold the object.
 */
static int
_vm_object_in_map(vm_map_t map, vm_object_t object, vm_map_entry_t entry)
{
	vm_map_t tmpm;
	vm_map_entry_t tmpe;
	vm_object_t obj, nobj;
	int entcount;

	if (map == 0)
		return 0;
	if (entry == 0) {
		tmpe = map->header.next;
		entcount = map->nentries;
		while (entcount-- && (tmpe != &map->header)) {
			if( _vm_object_in_map(map, object, tmpe)) {
				return 1;
			}
			tmpe = tmpe->next;
		}
		return (0);
	}
	switch(entry->maptype) {
	case VM_MAPTYPE_SUBMAP:
		tmpm = entry->object.sub_map;
		tmpe = tmpm->header.next;
		entcount = tmpm->nentries;
		while (entcount-- && tmpe != &tmpm->header) {
			if( _vm_object_in_map(tmpm, object, tmpe)) {
				return 1;
			}
			tmpe = tmpe->next;
		}
		break;
	case VM_MAPTYPE_NORMAL:
	case VM_MAPTYPE_VPAGETABLE:
		obj = entry->object.vm_object;
		while (obj) {
			if (obj == object) {
				if (obj != entry->object.vm_object)
					vm_object_drop(obj);
				return 1;
			}
			while ((nobj = obj->backing_object) != NULL) {
				vm_object_hold(nobj);
				if (nobj == obj->backing_object)
					break;
				vm_object_drop(nobj);
			}
			if (obj != entry->object.vm_object) {
				if (nobj)
					vm_object_lock_swap();
				vm_object_drop(obj);
			}
			obj = nobj;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int vm_object_in_map_callback(struct proc *p, void *data);

struct vm_object_in_map_info {
	vm_object_t object;
	int rv;
};

/*
 * Debugging only
 */
static int
vm_object_in_map(vm_object_t object)
{
	struct vm_object_in_map_info info;

	info.rv = 0;
	info.object = object;

	allproc_scan(vm_object_in_map_callback, &info);
	if (info.rv)
		return 1;
	if( _vm_object_in_map(&kernel_map, object, 0))
		return 1;
	if( _vm_object_in_map(&pager_map, object, 0))
		return 1;
	if( _vm_object_in_map(&buffer_map, object, 0))
		return 1;
	return 0;
}

/*
 * Debugging only
 */
static int
vm_object_in_map_callback(struct proc *p, void *data)
{
	struct vm_object_in_map_info *info = data;

	if (p->p_vmspace) {
		if (_vm_object_in_map(&p->p_vmspace->vm_map, info->object, 0)) {
			info->rv = 1;
			return -1;
		}
	}
	return (0);
}

DB_SHOW_COMMAND(vmochk, vm_object_check)
{
	vm_object_t object;

	/*
	 * make sure that internal objs are in a map somewhere
	 * and none have zero ref counts.
	 */
	for (object = TAILQ_FIRST(&vm_object_list);
			object != NULL;
			object = TAILQ_NEXT(object, object_list)) {
		if (object->type == OBJT_MARKER)
			continue;
		if (object->handle == NULL &&
		    (object->type == OBJT_DEFAULT || object->type == OBJT_SWAP)) {
			if (object->ref_count == 0) {
				db_printf("vmochk: internal obj has zero ref count: %ld\n",
					(long)object->size);
			}
			if (!vm_object_in_map(object)) {
				db_printf(
			"vmochk: internal obj is not in a map: "
			"ref: %d, size: %lu: 0x%lx, backing_object: %p\n",
				    object->ref_count, (u_long)object->size, 
				    (u_long)object->size,
				    (void *)object->backing_object);
			}
		}
	}
}

/*
 * Debugging only
 */
DB_SHOW_COMMAND(object, vm_object_print_static)
{
	/* XXX convert args. */
	vm_object_t object = (vm_object_t)addr;
	boolean_t full = have_addr;

	vm_page_t p;

	/* XXX count is an (unused) arg.  Avoid shadowing it. */
#define	count	was_count

	int count;

	if (object == NULL)
		return;

	db_iprintf(
	    "Object %p: type=%d, size=0x%lx, res=%d, ref=%d, flags=0x%x\n",
	    object, (int)object->type, (u_long)object->size,
	    object->resident_page_count, object->ref_count, object->flags);
	/*
	 * XXX no %qd in kernel.  Truncate object->backing_object_offset.
	 */
	db_iprintf(" sref=%d, backing_object(%d)=(%p)+0x%lx\n",
	    object->shadow_count, 
	    object->backing_object ? object->backing_object->ref_count : 0,
	    object->backing_object, (long)object->backing_object_offset);

	if (!full)
		return;

	db_indent += 2;
	count = 0;
	RB_FOREACH(p, vm_page_rb_tree, &object->rb_memq) {
		if (count == 0)
			db_iprintf("memory:=");
		else if (count == 6) {
			db_printf("\n");
			db_iprintf(" ...");
			count = 0;
		} else
			db_printf(",");
		count++;

		db_printf("(off=0x%lx,page=0x%lx)",
		    (u_long) p->pindex, (u_long) VM_PAGE_TO_PHYS(p));
	}
	if (count != 0)
		db_printf("\n");
	db_indent -= 2;
}

/* XXX. */
#undef count

/*
 * XXX need this non-static entry for calling from vm_map_print.
 *
 * Debugging only
 */
void
vm_object_print(/* db_expr_t */ long addr,
		boolean_t have_addr,
		/* db_expr_t */ long count,
		char *modif)
{
	vm_object_print_static(addr, have_addr, count, modif);
}

/*
 * Debugging only
 */
DB_SHOW_COMMAND(vmopag, vm_object_print_pages)
{
	vm_object_t object;
	int nl = 0;
	int c;
	for (object = TAILQ_FIRST(&vm_object_list);
			object != NULL;
			object = TAILQ_NEXT(object, object_list)) {
		vm_pindex_t idx, fidx;
		vm_pindex_t osize;
		vm_paddr_t pa = -1, padiff;
		int rcount;
		vm_page_t m;

		if (object->type == OBJT_MARKER)
			continue;
		db_printf("new object: %p\n", (void *)object);
		if ( nl > 18) {
			c = cngetc();
			if (c != ' ')
				return;
			nl = 0;
		}
		nl++;
		rcount = 0;
		fidx = 0;
		osize = object->size;
		if (osize > 128)
			osize = 128;
		for (idx = 0; idx < osize; idx++) {
			m = vm_page_lookup(object, idx);
			if (m == NULL) {
				if (rcount) {
					db_printf(" index(%ld)run(%d)pa(0x%lx)\n",
						(long)fidx, rcount, (long)pa);
					if ( nl > 18) {
						c = cngetc();
						if (c != ' ')
							return;
						nl = 0;
					}
					nl++;
					rcount = 0;
				}
				continue;
			}

				
			if (rcount &&
				(VM_PAGE_TO_PHYS(m) == pa + rcount * PAGE_SIZE)) {
				++rcount;
				continue;
			}
			if (rcount) {
				padiff = pa + rcount * PAGE_SIZE - VM_PAGE_TO_PHYS(m);
				padiff >>= PAGE_SHIFT;
				padiff &= PQ_L2_MASK;
				if (padiff == 0) {
					pa = VM_PAGE_TO_PHYS(m) - rcount * PAGE_SIZE;
					++rcount;
					continue;
				}
				db_printf(" index(%ld)run(%d)pa(0x%lx)",
					(long)fidx, rcount, (long)pa);
				db_printf("pd(%ld)\n", (long)padiff);
				if ( nl > 18) {
					c = cngetc();
					if (c != ' ')
						return;
					nl = 0;
				}
				nl++;
			}
			fidx = idx;
			pa = VM_PAGE_TO_PHYS(m);
			rcount = 1;
		}
		if (rcount) {
			db_printf(" index(%ld)run(%d)pa(0x%lx)\n",
				(long)fidx, rcount, (long)pa);
			if ( nl > 18) {
				c = cngetc();
				if (c != ' ')
					return;
				nl = 0;
			}
			nl++;
		}
	}
}
#endif /* DDB */
