/*
 * Copyright (c) 1991, 1993, 2013
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
#include <sys/malloc.h>
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

struct vm_object kernel_object;

struct vm_object_hash vm_object_hash[VMOBJ_HSIZE];

MALLOC_DEFINE(M_VM_OBJECT, "vm_object", "vm_object structures");

#define VMOBJ_HASH_PRIME1	66555444443333333ULL
#define VMOBJ_HASH_PRIME2	989042931893ULL

int vm_object_debug;
SYSCTL_INT(_vm, OID_AUTO, object_debug, CTLFLAG_RW, &vm_object_debug, 0, "");

static __inline
struct vm_object_hash *
vmobj_hash(vm_object_t obj)
{
	uintptr_t hash1;
	uintptr_t hash2;

	hash1 = (uintptr_t)obj + ((uintptr_t)obj >> 18);
	hash1 %= VMOBJ_HASH_PRIME1;
	hash2 = ((uintptr_t)obj >> 8) + ((uintptr_t)obj >> 24);
	hash2 %= VMOBJ_HASH_PRIME2;
	return (&vm_object_hash[(hash1 ^ hash2) & VMOBJ_HMASK]);
}

#if defined(DEBUG_LOCKS)

#define vm_object_vndeallocate(obj, vpp)	\
                debugvm_object_vndeallocate(obj, vpp, __FILE__, __LINE__)

/*
 * Debug helper to track hold/drop/ref/deallocate calls.
 */
static void
debugvm_object_add(vm_object_t obj, char *file, int line, int addrem)
{
	int i;

	i = atomic_fetchadd_int(&obj->debug_index, 1);
	i = i & (VMOBJ_DEBUG_ARRAY_SIZE - 1);
	ksnprintf(obj->debug_hold_thrs[i],
		  sizeof(obj->debug_hold_thrs[i]),
		  "%c%d:(%d):%s",
		  (addrem == -1 ? '-' : (addrem == 1 ? '+' : '=')),
		  (curthread->td_proc ? curthread->td_proc->p_pid : -1),
		  obj->ref_count,
		  curthread->td_comm);
	obj->debug_hold_file[i] = file;
	obj->debug_hold_line[i] = line;
#if 0
	/* Uncomment for debugging obj refs/derefs in reproducable cases */
	if (strcmp(curthread->td_comm, "sshd") == 0) {
		kprintf("%d %p refs=%d ar=%d file: %s/%d\n",
			(curthread->td_proc ? curthread->td_proc->p_pid : -1),
			obj, obj->ref_count, addrem, file, line);
	}
#endif
}

#endif

/*
 * Misc low level routines
 */
static void
vm_object_lock_init(vm_object_t obj)
{
#if defined(DEBUG_LOCKS)
	int i;

	obj->debug_index = 0;
	for (i = 0; i < VMOBJ_DEBUG_ARRAY_SIZE; i++) {
		obj->debug_hold_thrs[i][0] = 0;
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

void
vm_object_upgrade(vm_object_t obj)
{
	lwkt_reltoken(&obj->token);
	lwkt_gettoken(&obj->token);
}

void
vm_object_downgrade(vm_object_t obj)
{
	lwkt_reltoken(&obj->token);
	lwkt_gettoken_shared(&obj->token);
}

static __inline void
vm_object_assert_held(vm_object_t obj)
{
	ASSERT_LWKT_TOKEN_HELD(&obj->token);
}

int
vm_quickcolor(void)
{
	globaldata_t gd = mycpu;
	int pg_color;

	pg_color = (int)(intptr_t)gd->gd_curthread >> 10;
	pg_color += gd->gd_quick_color;
	gd->gd_quick_color += PQ_PRIME2;

	return pg_color;
}

void
VMOBJDEBUG(vm_object_hold)(vm_object_t obj VMOBJDBARGS)
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
	debugvm_object_add(obj, file, line, 1);
#endif
}

int
VMOBJDEBUG(vm_object_hold_try)(vm_object_t obj VMOBJDBARGS)
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
				kfree(obj, M_VM_OBJECT);
		}
		return(0);
	}

#if defined(DEBUG_LOCKS)
	debugvm_object_add(obj, file, line, 1);
#endif
	return(1);
}

void
VMOBJDEBUG(vm_object_hold_shared)(vm_object_t obj VMOBJDBARGS)
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
	debugvm_object_add(obj, file, line, 1);
#endif
}

/*
 * Drop the token and hold_count on the object.
 *
 * WARNING! Token might be shared.
 */
void
VMOBJDEBUG(vm_object_drop)(vm_object_t obj VMOBJDBARGS)
{
	if (obj == NULL)
		return;

	/*
	 * No new holders should be possible once we drop hold_count 1->0 as
	 * there is no longer any way to reference the object.
	 */
	KKASSERT(obj->hold_count > 0);
	if (refcount_release(&obj->hold_count)) {
#if defined(DEBUG_LOCKS)
		debugvm_object_add(obj, file, line, -1);
#endif

		if (obj->ref_count == 0 && (obj->flags & OBJ_DEAD)) {
			vm_object_unlock(obj);
			kfree(obj, M_VM_OBJECT);
		} else {
			vm_object_unlock(obj);
		}
	} else {
#if defined(DEBUG_LOCKS)
		debugvm_object_add(obj, file, line, -1);
#endif
		vm_object_unlock(obj);
	}
}

/*
 * Initialize a freshly allocated object, returning a held object.
 *
 * Used only by vm_object_allocate(), zinitna() and vm_object_init().
 *
 * No requirements.
 */
void
_vm_object_allocate(objtype_t type, vm_pindex_t size, vm_object_t object,
		    const char *ident)
{
	struct vm_object_hash *hash;

	RB_INIT(&object->rb_memq);
	lwkt_token_init(&object->token, ident);

	TAILQ_INIT(&object->backing_list);
	lockinit(&object->backing_lk, "baclk", 0, 0);

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
	/* cpu localization twist */
	object->pg_color = vm_quickcolor();
	object->handle = NULL;

	atomic_add_int(&object->generation, 1);
	object->swblock_count = 0;
	RB_INIT(&object->swblock_root);
	vm_object_lock_init(object);
	pmap_object_init(object);

	vm_object_hold(object);

	hash = vmobj_hash(object);
	lwkt_gettoken(&hash->token);
	TAILQ_INSERT_TAIL(&hash->list, object, object_entry);
	lwkt_reltoken(&hash->token);
}

/*
 * Initialize a VM object.
 */
void
vm_object_init(vm_object_t object, vm_pindex_t size)
{
	_vm_object_allocate(OBJT_DEFAULT, size, object, "vmobj");
	vm_object_drop(object);
}

/*
 * Initialize the VM objects module.
 *
 * Called from the low level boot code only.  Note that this occurs before
 * kmalloc is initialized so we cannot allocate any VM objects.
 */
void
vm_object_init1(void)
{
	int i;

	for (i = 0; i < VMOBJ_HSIZE; ++i) {
		TAILQ_INIT(&vm_object_hash[i].list);
		lwkt_token_init(&vm_object_hash[i].token, "vmobjlst");
	}

	_vm_object_allocate(OBJT_DEFAULT, OFF_TO_IDX(KvaEnd),
			    &kernel_object, "kobj");
	vm_object_drop(&kernel_object);
}

void
vm_object_init2(void)
{
	kmalloc_set_unlimited(M_VM_OBJECT);
}

/*
 * Allocate and return a new object of the specified type and size.
 *
 * No requirements.
 */
vm_object_t
vm_object_allocate(objtype_t type, vm_pindex_t size)
{
	vm_object_t obj;

	obj = kmalloc(sizeof(*obj), M_VM_OBJECT, M_INTWAIT|M_ZERO);
	_vm_object_allocate(type, size, obj, "vmobj");
	vm_object_drop(obj);

	return (obj);
}

/*
 * This version returns a held object, allowing further atomic initialization
 * of the object.
 */
vm_object_t
vm_object_allocate_hold(objtype_t type, vm_pindex_t size)
{
	vm_object_t obj;

	obj = kmalloc(sizeof(*obj), M_VM_OBJECT, M_INTWAIT|M_ZERO);
	_vm_object_allocate(type, size, obj, "vmobj");

	return (obj);
}

/*
 * Add an additional reference to a vm_object.  The object must already be
 * held.  The original non-lock version is no longer supported.  The object
 * must NOT be chain locked by anyone at the time the reference is added.
 *
 * The object must be held, but may be held shared if desired (hence why
 * we use an atomic op).
 */
void
VMOBJDEBUG(vm_object_reference_locked)(vm_object_t object VMOBJDBARGS)
{
	KKASSERT(object != NULL);
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	atomic_add_int(&object->ref_count, 1);
	if (object->type == OBJT_VNODE) {
		vref(object->handle);
		/* XXX what if the vnode is being destroyed? */
	}
#if defined(DEBUG_LOCKS)
	debugvm_object_add(object, file, line, 1);
#endif
}

/*
 * This version is only allowed in situations where the caller
 * already knows that the object is deterministically referenced
 * (usually because its taken from a ref'd vnode, or during a map_entry
 * replication).
 */
void
VMOBJDEBUG(vm_object_reference_quick)(vm_object_t object VMOBJDBARGS)
{
	KKASSERT(object->type == OBJT_VNODE || object->ref_count > 0);
	atomic_add_int(&object->ref_count, 1);
	if (object->type == OBJT_VNODE)
		vref(object->handle);
#if defined(DEBUG_LOCKS)
	debugvm_object_add(object, file, line, 1);
#endif
}

/*
 * Dereference an object and its underlying vnode.  The object may be
 * held shared.  On return the object will remain held.
 *
 * This function may return a vnode in *vpp which the caller must release
 * after the caller drops its own lock.  If vpp is NULL, we assume that
 * the caller was holding an exclusive lock on the object and we vrele()
 * the vp ourselves.
 */
static void
VMOBJDEBUG(vm_object_vndeallocate)(vm_object_t object, struct vnode **vpp
				   VMOBJDBARGS)
{
	struct vnode *vp = (struct vnode *) object->handle;
	int count;

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
	count = object->ref_count;
	cpu_ccfence();
	for (;;) {
		if (count == 1) {
			vm_object_upgrade(object);
			if (atomic_fcmpset_int(&object->ref_count, &count, 0)) {
				vclrflags(vp, VTEXT);
				break;
			}
		} else {
			if (atomic_fcmpset_int(&object->ref_count,
					       &count, count - 1)) {
				break;
			}
		}
		cpu_pause();
		/* retry */
	}
#if defined(DEBUG_LOCKS)
	debugvm_object_add(object, file, line, -1);
#endif

	/*
	 * vrele or return the vp to vrele.  We can only safely vrele(vp)
	 * if the object was locked exclusively.  But there are two races
	 * here.
	 *
	 * We had to upgrade the object above to safely clear VTEXT
	 * but the alternative path where the shared lock is retained
	 * can STILL race to 0 in other paths and cause our own vrele()
	 * to terminate the vnode.  We can't allow that if the VM object
	 * is still locked shared.
	 */
	if (vpp)
		*vpp = vp;
	else
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
VMOBJDEBUG(vm_object_deallocate)(vm_object_t object VMOBJDBARGS)
{
	struct vnode *vp;
	int count;

	if (object == NULL)
		return;

	count = object->ref_count;
	cpu_ccfence();
	for (;;) {
		/*
		 * If decrementing the count enters into special handling
		 * territory (0, 1, or 2) we have to do it the hard way.
		 * Fortunate though, objects with only a few refs like this
		 * are not likely to be heavily contended anyway.
		 *
		 * For vnode objects we only care about 1->0 transitions.
		 */
		if (count <= 3 || (object->type == OBJT_VNODE && count <= 1)) {
#if defined(DEBUG_LOCKS)
			debugvm_object_add(object, file, line, 0);
#endif
			vm_object_hold(object);
			vm_object_deallocate_locked(object);
			vm_object_drop(object);
			break;
		}

		/*
		 * Try to decrement ref_count without acquiring a hold on
		 * the object.  This is particularly important for the exec*()
		 * and exit*() code paths because the program binary may
		 * have a great deal of sharing and an exclusive lock will
		 * crowbar performance in those circumstances.
		 */
		if (object->type == OBJT_VNODE) {
			vp = (struct vnode *)object->handle;
			if (atomic_fcmpset_int(&object->ref_count,
					       &count, count - 1)) {
#if defined(DEBUG_LOCKS)
				debugvm_object_add(object, file, line, -1);
#endif

				vrele(vp);
				break;
			}
			/* retry */
		} else {
			if (atomic_fcmpset_int(&object->ref_count,
					       &count, count - 1)) {
#if defined(DEBUG_LOCKS)
				debugvm_object_add(object, file, line, -1);
#endif
				break;
			}
			/* retry */
		}
		cpu_pause();
		/* retry */
	}
}

void
VMOBJDEBUG(vm_object_deallocate_locked)(vm_object_t object VMOBJDBARGS)
{
	/*
	 * Degenerate case
	 */
	if (object == NULL)
		return;

	/*
	 * vnode case, caller either locked the object exclusively
	 * or this is a recursion with must_drop != 0 and the vnode
	 * object will be locked shared.
	 *
	 * If locked shared we have to drop the object before we can
	 * call vrele() or risk a shared/exclusive livelock.
	 */
	if (object->type == OBJT_VNODE) {
		ASSERT_LWKT_TOKEN_HELD(&object->token);
		vm_object_vndeallocate(object, NULL);
		return;
	}
	ASSERT_LWKT_TOKEN_HELD_EXCL(&object->token);

	/*
	 * Normal case (object is locked exclusively)
	 */
	if (object->ref_count == 0) {
		panic("vm_object_deallocate: object deallocated "
		      "too many times: %d", object->type);
	}
	if (object->ref_count > 2) {
		atomic_add_int(&object->ref_count, -1);
#if defined(DEBUG_LOCKS)
		debugvm_object_add(object, file, line, -1);
#endif
		return;
	}

	/*
	 * Drop the ref and handle termination on the 1->0 transition.
	 * We may have blocked above so we have to recheck.
	 */
	KKASSERT(object->ref_count != 0);
	if (object->ref_count >= 2) {
		atomic_add_int(&object->ref_count, -1);
#if defined(DEBUG_LOCKS)
		debugvm_object_add(object, file, line, -1);
#endif
		return;
	}

	atomic_add_int(&object->ref_count, -1);
	if ((object->flags & OBJ_DEAD) == 0)
		vm_object_terminate(object);
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
	struct rb_vm_page_scan_info info;
	struct vm_object_hash *hash;

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
	info.count = 0;
	info.object = object;
	do {
		info.error = 0;
		vm_page_rb_tree_RB_SCAN(&object->rb_memq, NULL,
					vm_object_terminate_callback, &info);
	} while (info.error);

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
			"still has %ld pages\n",
			object, object->resident_page_count);
		vm_page_rb_tree_RB_SCAN(&object->rb_memq, NULL,
					vm_object_terminate_callback, &info);
	}

	/*
	 * There had better not be any pages left
	 */
	KKASSERT(object->resident_page_count == 0);

	/*
	 * Remove the object from the global object list.
	 */
	hash = vmobj_hash(object);
	lwkt_gettoken(&hash->token);
	TAILQ_REMOVE(&hash->list, object, object_entry);
	lwkt_reltoken(&hash->token);

	if (object->ref_count != 0) {
		panic("vm_object_terminate2: object with references, "
		      "ref_count=%d", object->ref_count);
	}

	/*
	 * NOTE: The object hold_count is at least 1, so we cannot kfree()
	 *	 the object here.  See vm_object_drop().
	 */
}

/*
 * The caller must hold the object.
 *
 * NOTE: It is possible for vm_page's to remain flagged PG_MAPPED
 *	 or PG_MAPPED|PG_WRITEABLE, even after pmap_mapped_sync()
 *	 is called, due to normal pmap operations.  This is because only
 *	 global pmap operations on the vm_page can clear the bits and not
 *	 just local operations on individual pmaps.
 *
 *	 Most interactions that necessitate the clearing of these bits
 *	 proactively call vm_page_protect(), and we must do so here as well.
 */
static int
vm_object_terminate_callback(vm_page_t p, void *data)
{
	struct rb_vm_page_scan_info *info = data;
	vm_object_t object;

	object = p->object;
	KKASSERT(object == info->object);
	if (vm_page_busy_try(p, TRUE)) {
		vm_page_sleep_busy(p, TRUE, "vmotrm");
		info->error = 1;
		return 0;
	}
	if (object != p->object) {
		/* XXX remove once we determine it can't happen */
		kprintf("vm_object_terminate: Warning: Encountered "
			"busied page %p on queue %d\n", p, p->queue);
		vm_page_wakeup(p);
		info->error = 1;
	} else if (p->wire_count == 0) {
		/*
		 * NOTE: p->dirty and PG_NEED_COMMIT are ignored.
		 */
		if (pmap_mapped_sync(p) & (PG_MAPPED | PG_WRITEABLE))
			vm_page_protect(p, VM_PROT_NONE);
		vm_page_free(p);
		mycpu->gd_cnt.v_pfree++;
	} else {
		if (p->queue != PQ_NONE) {
			kprintf("vm_object_terminate: Warning: Encountered "
				"wired page %p on queue %d\n", p, p->queue);
			if (vm_object_debug > 0) {
				--vm_object_debug;
				print_backtrace(10);
			}
		}
		if (pmap_mapped_sync(p) & (PG_MAPPED | PG_WRITEABLE))
			vm_page_protect(p, VM_PROT_NONE);
		vm_page_remove(p);
		vm_page_wakeup(p);
	}

	/*
	 * Must be at end to avoid SMP races, caller holds object token
	 */
	if ((++info->count & 63) == 0)
		lwkt_user_yield();
	return(0);
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
			OBJPC_SYNC : OBJPC_CLUSTER_OK;
	pagerflags |= (flags & OBJPC_INVAL) ? OBJPC_INVAL : 0;

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
		info.count = 0;
		vm_page_rb_tree_RB_SCAN(&object->rb_memq, rb_vm_page_scancmp,
					vm_object_page_clean_pass1, &info);
		if (info.error == 0) {
			vm_object_clear_flag(object,
					     OBJ_WRITEABLE|OBJ_MIGHTBEDIRTY);
			if (object->type == OBJT_VNODE &&
			    (vp = (struct vnode *)object->handle) != NULL) {
				/*
				 * Use new-style interface to clear VISDIRTY
				 * because the vnode is not necessarily removed
				 * from the syncer list(s) as often as it was
				 * under the old interface, which can leave
				 * the vnode on the syncer list after reclaim.
				 */
				vclrobjdirty(vp);
			}
		}
	}

	/*
	 * Do a pass to clean all the dirty pages we find.
	 */
	do {
		info.error = 0;
		info.count = 0;
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

	KKASSERT(p->object == info->object);

	vm_page_flag_set(p, PG_CLEANCHK);
	if ((info->limit & OBJPC_NOSYNC) && (p->flags & PG_NOSYNC)) {
		info->error = 1;
	} else if (vm_page_busy_try(p, FALSE)) {
		info->error = 1;
	} else {
		KKASSERT(p->object == info->object);
		vm_page_protect(p, VM_PROT_READ);
		vm_page_wakeup(p);
	}

	/*
	 * Must be at end to avoid SMP races, caller holds object token
	 */
	if ((++info->count & 63) == 0)
		lwkt_user_yield();
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

	KKASSERT(p->object == info->object);

	/*
	 * Do not mess with pages that were inserted after we started
	 * the cleaning pass.
	 */
	if ((p->flags & PG_CLEANCHK) == 0)
		goto done;

	generation = info->object->generation;

	if (vm_page_busy_try(p, TRUE)) {
		vm_page_sleep_busy(p, TRUE, "vpcwai");
		info->error = 1;
		goto done;
	}

	KKASSERT(p->object == info->object &&
		 info->object->generation == generation);

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
	/* vm_wait_nominal(); this can deadlock the system in syncer/pageout */

	/*
	 * Must be at end to avoid SMP races, caller holds object token
	 */
done:
	if ((++info->count & 63) == 0)
		lwkt_user_yield();
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
		if ((pagerflags & OBJPC_IGNORE_CLEANCHK) == 0 &&
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
		if ((pagerflags & OBJPC_IGNORE_CLEANCHK) == 0 &&
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
vm_object_madvise(vm_object_t object, vm_pindex_t pindex,
		  vm_pindex_t count, int advise)
{
	vm_pindex_t end;
	vm_page_t m;
	int error;

	if (object == NULL)
		return;

	end = pindex + count;

	vm_object_hold(object);

	/*
	 * Locate and adjust resident pages.  This only applies to the
	 * primary object in the mapping.
	 */
	for (; pindex < end; pindex += 1) {
relookup:
		/*
		 * MADV_FREE only operates on OBJT_DEFAULT or OBJT_SWAP pages
		 * and those pages must be OBJ_ONEMAPPING.
		 */
		if (advise == MADV_FREE) {
			if ((object->type != OBJT_DEFAULT &&
			     object->type != OBJT_SWAP) ||
			    (object->flags & OBJ_ONEMAPPING) == 0) {
				continue;
			}
		}

		m = vm_page_lookup_busy_try(object, pindex, TRUE, &error);

		if (error) {
			vm_page_sleep_busy(m, TRUE, "madvpo");
			goto relookup;
		}
		if (m == NULL) {
			/*
			 * There may be swap even if there is no backing page
			 */
			if (advise == MADV_FREE && object->type == OBJT_SWAP)
				swap_pager_freespace(object, pindex, 1);
			continue;
		}

		/*
		 * If the page is not in a normal active state, we skip it.
		 * If the page is not managed there are no page queues to
		 * mess with.  Things can break if we mess with pages in
		 * any of the below states.
		 */
		if (m->wire_count ||
		    (m->flags & (PG_FICTITIOUS | PG_UNQUEUED |
				 PG_NEED_COMMIT)) ||
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
			if (object->type == OBJT_SWAP)
				swap_pager_freespace(object, pindex, 1);
		}
		vm_page_wakeup(m);
	}
	vm_object_drop(object);
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
	info.object = object;
	info.start_pindex = start;
	if (end == 0)
		info.end_pindex = (vm_pindex_t)-1;
	else
		info.end_pindex = end - 1;
	info.limit = clean_only;
	info.count = 0;
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
 * The caller must hold the object.
 *
 * NOTE: User yields are allowed when removing more than one page, but not
 *	 allowed if only removing one page (the path for single page removals
 *	 might hold a spinlock).
 */
static int
vm_object_page_remove_callback(vm_page_t p, void *data)
{
	struct rb_vm_page_scan_info *info = data;

	if (info->object != p->object ||
	    p->pindex < info->start_pindex ||
	    p->pindex > info->end_pindex) {
		kprintf("vm_object_page_remove_callbackA: obj/pg race %p/%p\n",
			info->object, p);
		return(0);
	}
	if (vm_page_busy_try(p, TRUE)) {
		vm_page_sleep_busy(p, TRUE, "vmopar");
		info->error = 1;
		return(0);
	}
	if (info->object != p->object) {
		/* this should never happen */
		kprintf("vm_object_page_remove_callbackB: obj/pg race %p/%p\n",
			info->object, p);
		vm_page_wakeup(p);
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
		goto done;
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
			goto done;
		}
	}

	/*
	 * Destroy the page.  But we have to re-test whether its dirty after
	 * removing it from its pmaps.
	 */
	vm_page_protect(p, VM_PROT_NONE);
	if (info->limit && p->valid) {
		vm_page_test_dirty(p);
		if ((p->valid & p->dirty) || (p->flags & PG_NEED_COMMIT)) {
			vm_page_wakeup(p);
			goto done;
		}
	}
	vm_page_free(p);

	/*
	 * Must be at end to avoid SMP races, caller holds object token
	 */
done:
	if ((++info->count & 63) == 0)
		lwkt_user_yield();

	return(0);
}

/*
 * Try to extend prev_object into an adjoining region of virtual
 * memory, return TRUE on success.
 *
 * The caller does not need to hold (prev_object) but must have a stable
 * pointer to it (typically by holding the vm_map locked).
 *
 * This function only works for anonymous memory objects which either
 * have (a) one reference or (b) we are extending the object's size.
 * Otherwise the related VM pages we want to use for the object might
 * be in use by another mapping.
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

#if 0
	/* caller now checks this */
	/*
	 * Try to collapse the object first
	 */
	vm_object_collapse(prev_object, NULL);
#endif

#if 0
	/* caller now checks this */
	/*
	 * We can't coalesce if we shadow another object (figuring out the
	 * relationships become too complex).
	 */
	if (prev_object->backing_object != NULL) {
		vm_object_chain_release(prev_object);
		vm_object_drop(prev_object);
		return (FALSE);
	}
#endif

	prev_size >>= PAGE_SHIFT;
	next_size >>= PAGE_SHIFT;
	next_pindex = prev_pindex + prev_size;

	/*
	 * We can't if the object has more than one ref count unless we
	 * are extending it into newly minted space.
	 */
	if (prev_object->ref_count > 1 &&
	    prev_object->size != next_pindex) {
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
	vm_object_drop(prev_object);

	return (TRUE);
}

/*
 * Make the object writable and flag is being possibly dirty.
 *
 * The object might not be held (or might be held but held shared),
 * the related vnode is probably not held either.  Object and vnode are
 * stable by virtue of the vm_page busied by the caller preventing
 * destruction.
 *
 * If the related mount is flagged MNTK_THR_SYNC we need to call
 * vsetobjdirty().  Filesystems using this option usually shortcut
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
			if (vp->v_mount &&
			    (vp->v_mount->mnt_kern_flag & MNTK_THR_SYNC)) {
				/*
				 * New style THR_SYNC places vnodes on the
				 * syncer list more deterministically.
				 */
				vsetobjdirty(vp);
			} else {
				/*
				 * Old style scan would not necessarily place
				 * a vnode on the syncer list when possibly
				 * modified via mmap.
				 */
				vsetflags(vp, VOBJDIRTY);
			}
		}
	}
}

#include "opt_ddb.h"
#ifdef DDB
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
	vm_map_backing_t ba;
	vm_map_t tmpm;
	vm_map_entry_t tmpe;
	int entcount;

	if (map == NULL)
		return 0;
	if (entry == NULL) {
		tmpe = RB_MIN(vm_map_rb_tree, &map->rb_root);
		entcount = map->nentries;
		while (entcount-- && tmpe) {
			if( _vm_object_in_map(map, object, tmpe)) {
				return 1;
			}
			tmpe = vm_map_rb_tree_RB_NEXT(tmpe);
		}
		return (0);
	}
	switch(entry->maptype) {
	case VM_MAPTYPE_SUBMAP:
		tmpm = entry->ba.sub_map;
		tmpe = RB_MIN(vm_map_rb_tree, &tmpm->rb_root);
		entcount = tmpm->nentries;
		while (entcount-- && tmpe) {
			if( _vm_object_in_map(tmpm, object, tmpe)) {
				return 1;
			}
			tmpe = vm_map_rb_tree_RB_NEXT(tmpe);
		}
		break;
	case VM_MAPTYPE_NORMAL:
	case VM_MAPTYPE_VPAGETABLE:
		ba = &entry->ba;
		while (ba) {
			if (ba->object == object)
				return TRUE;
			ba = ba->backing_ba;
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

	allproc_scan(vm_object_in_map_callback, &info, 0);
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
	struct vm_object_hash *hash;
	vm_object_t object;
	int n;

	/*
	 * make sure that internal objs are in a map somewhere
	 * and none have zero ref counts.
	 */
	for (n = 0; n < VMOBJ_HSIZE; ++n) {
		hash = &vm_object_hash[n];
		for (object = TAILQ_FIRST(&hash->list);
				object != NULL;
				object = TAILQ_NEXT(object, object_entry)) {
			if (object->type == OBJT_MARKER)
				continue;
			if (object->handle != NULL ||
			    (object->type != OBJT_DEFAULT &&
			     object->type != OBJT_SWAP)) {
				continue;
			}
			if (object->ref_count == 0) {
				db_printf("vmochk: internal obj has "
					  "zero ref count: %ld\n",
					  (long)object->size);
			}
			if (vm_object_in_map(object))
				continue;
			db_printf("vmochk: internal obj is not in a map: "
				  "ref: %d, size: %lu: 0x%lx\n",
				  object->ref_count, (u_long)object->size,
				  (u_long)object->size);
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
	    "Object %p: type=%d, size=0x%lx, res=%ld, ref=%d, flags=0x%x\n",
	    object, (int)object->type, (u_long)object->size,
	    object->resident_page_count, object->ref_count, object->flags);
	/*
	 * XXX no %qd in kernel.  Truncate object->backing_object_offset.
	 */
	db_iprintf("\n");

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
	struct vm_object_hash *hash;
	vm_object_t object;
	int nl = 0;
	int c;
	int n;

	for (n = 0; n < VMOBJ_HSIZE; ++n) {
		hash = &vm_object_hash[n];
		for (object = TAILQ_FIRST(&hash->list);
				object != NULL;
				object = TAILQ_NEXT(object, object_entry)) {
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
}
#endif /* DDB */
