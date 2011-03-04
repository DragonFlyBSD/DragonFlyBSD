/*
 * (MPSAFE)
 *
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

#define EASY_SCAN_FACTOR	8

static void	vm_object_qcollapse(vm_object_t object);
static int	vm_object_page_collect_flush(vm_object_t object, vm_page_t p,
					     int pagerflags);
static void	vm_object_lock_init(vm_object_t);
static void	vm_object_hold_wake(vm_object_t);
static void	vm_object_hold_wait(vm_object_t);


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
extern int vm_pageout_page_count;

static long object_collapses;
static long object_bypasses;
static int next_index;
static vm_zone_t obj_zone;
static struct vm_zone obj_zone_store;
#define VM_OBJECTS_INIT 256
static struct vm_object vm_objects_init[VM_OBJECTS_INIT];

/*
 * Initialize a freshly allocated object
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

	object->type = type;
	object->size = size;
	object->ref_count = 1;
	object->hold_count = 0;
	object->flags = 0;
	if ((object->type == OBJT_DEFAULT) || (object->type == OBJT_SWAP))
		vm_object_set_flag(object, OBJ_ONEMAPPING);
	object->paging_in_progress = 0;
	object->resident_page_count = 0;
	object->agg_pv_list_count = 0;
	object->shadow_count = 0;
	object->pg_color = next_index;
	if ( size > (PQ_L2_SIZE / 3 + PQ_PRIME1))
		incr = PQ_L2_SIZE / 3 + PQ_PRIME1;
	else
		incr = size;
	next_index = (next_index + incr) & PQ_L2_MASK;
	object->handle = NULL;
	object->backing_object = NULL;
	object->backing_object_offset = (vm_ooffset_t) 0;

	object->generation++;
	object->swblock_count = 0;
	RB_INIT(&object->swblock_root);
	vm_object_lock_init(object);

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

	return (result);
}

/*
 * Add an additional reference to a vm_object.
 *
 * Object passed by caller must be stable or caller must already
 * hold vmobj_token to avoid races.
 */
void
vm_object_reference(vm_object_t object)
{
	lwkt_gettoken(&vmobj_token);
	vm_object_reference_locked(object);
	lwkt_reltoken(&vmobj_token);
}

void
vm_object_reference_locked(vm_object_t object)
{
	if (object) {
		ASSERT_LWKT_TOKEN_HELD(&vmobj_token);
		object->ref_count++;
		if (object->type == OBJT_VNODE) {
			vref(object->handle);
			/* XXX what if the vnode is being destroyed? */
		}
	}
}

/*
 * Dereference an object and its underlying vnode.
 *
 * The caller must hold vmobj_token.
 */
static void
vm_object_vndeallocate(vm_object_t object)
{
	struct vnode *vp = (struct vnode *) object->handle;

	KASSERT(object->type == OBJT_VNODE,
	    ("vm_object_vndeallocate: not a vnode object"));
	KASSERT(vp != NULL, ("vm_object_vndeallocate: missing vp"));
	ASSERT_LWKT_TOKEN_HELD(&vmobj_token);
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
 */
void
vm_object_deallocate(vm_object_t object)
{
	lwkt_gettoken(&vmobj_token);
	vm_object_deallocate_locked(object);
	lwkt_reltoken(&vmobj_token);
}

void
vm_object_deallocate_locked(vm_object_t object)
{
	vm_object_t temp;

	ASSERT_LWKT_TOKEN_HELD(&vmobj_token);

	while (object != NULL) {
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
		 * We currently need the vm_token from this point on, and
		 * we must recheck ref_count after acquiring it.
		 */
		lwkt_gettoken(&vm_token);

		if (object->ref_count > 2) {
			object->ref_count--;
			lwkt_reltoken(&vm_token);
			break;
		}

		/*
		 * Here on ref_count of one or two, which are special cases for
		 * objects.
		 */
		if ((object->ref_count == 2) && (object->shadow_count == 0)) {
			vm_object_set_flag(object, OBJ_ONEMAPPING);
			object->ref_count--;
			lwkt_reltoken(&vm_token);
			break;
		}
		if ((object->ref_count == 2) && (object->shadow_count == 1)) {
			object->ref_count--;
			if ((object->handle == NULL) &&
			    (object->type == OBJT_DEFAULT ||
			     object->type == OBJT_SWAP)) {
				vm_object_t robject;

				robject = LIST_FIRST(&object->shadow_head);
				KASSERT(robject != NULL,
					("vm_object_deallocate: ref_count: "
					"%d, shadow_count: %d",
					object->ref_count,
					object->shadow_count));

				if ((robject->handle == NULL) &&
				    (robject->type == OBJT_DEFAULT ||
				     robject->type == OBJT_SWAP)) {

					robject->ref_count++;

					while (
						robject->paging_in_progress ||
						object->paging_in_progress
					) {
						vm_object_pip_sleep(robject, "objde1");
						vm_object_pip_sleep(object, "objde2");
					}

					if (robject->ref_count == 1) {
						robject->ref_count--;
						object = robject;
						goto doterm;
					}

					object = robject;
					vm_object_collapse(object);
					lwkt_reltoken(&vm_token);
					continue;
				}
			}
			lwkt_reltoken(&vm_token);
			break;
		}

		/*
		 * Normal dereferencing path
		 */
		object->ref_count--;
		if (object->ref_count != 0) {
			lwkt_reltoken(&vm_token);
			break;
		}

		/*
		 * Termination path
		 */
doterm:
		temp = object->backing_object;
		if (temp) {
			LIST_REMOVE(object, shadow_list);
			temp->shadow_count--;
			temp->generation++;
			object->backing_object = NULL;
		}
		lwkt_reltoken(&vm_token);

		/*
		 * Don't double-terminate, we could be in a termination
		 * recursion due to the terminate having to sync data
		 * to disk.
		 */
		if ((object->flags & OBJ_DEAD) == 0)
			vm_object_terminate(object);
		object = temp;
	}
}

/*
 * Destroy the specified object, freeing up related resources.
 *
 * The object must have zero references.
 *
 * The caller must be holding vmobj_token and properly interlock with
 * OBJ_DEAD.
 */
static int vm_object_terminate_callback(vm_page_t p, void *data);

void
vm_object_terminate(vm_object_t object)
{
	/*
	 * Make sure no one uses us.  Once we set OBJ_DEAD we should be
	 * able to safely block.
	 */
	KKASSERT((object->flags & OBJ_DEAD) == 0);
	ASSERT_LWKT_TOKEN_HELD(&vmobj_token);
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
		 */
		vm_object_page_clean(object, 0, 0, OBJPC_SYNC);

		vp = (struct vnode *) object->handle;
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
	 * Now free any remaining pages. For internal objects, this also
	 * removes them from paging queues. Don't free wired pages, just
	 * remove them from the object. 
	 */
	lwkt_gettoken(&vm_token);
	vm_page_rb_tree_RB_SCAN(&object->rb_memq, NULL,
				vm_object_terminate_callback, NULL);
	lwkt_reltoken(&vm_token);

	/*
	 * Let the pager know object is dead.
	 */
	vm_pager_deallocate(object);

	/*
	 * Wait for the object hold count to hit zero
	 */
	vm_object_hold_wait(object);

	/*
	 * Remove the object from the global object list.
	 *
	 * (we are holding vmobj_token)
	 */
	TAILQ_REMOVE(&vm_object_list, object, object_list);
	vm_object_count--;
	vm_object_dead_wakeup(object);

	if (object->ref_count != 0) {
		panic("vm_object_terminate2: object with references, "
		      "ref_count=%d", object->ref_count);
	}

	/*
	 * Free the space for the object.
	 */
	zfree(obj_zone, object);
}

/*
 * The caller must hold vm_token.
 */
static int
vm_object_terminate_callback(vm_page_t p, void *data __unused)
{
	if (p->busy || (p->flags & PG_BUSY))
		panic("vm_object_terminate: freeing busy page %p", p);
	if (p->wire_count == 0) {
		vm_page_busy(p);
		vm_page_free(p);
		mycpu->gd_cnt.v_pfree++;
	} else {
		if (p->queue != PQ_NONE)
			kprintf("vm_object_terminate: Warning: Encountered wired page %p on queue %d\n", p, p->queue);
		vm_page_busy(p);
		vm_page_remove(p);
		vm_page_wakeup(p);
	}
	return(0);
}

/*
 * The object is dead but still has an object<->pager association.  Sleep
 * and return.  The caller typically retests the association in a loop.
 *
 * Must be called with the vmobj_token held.
 */
void
vm_object_dead_sleep(vm_object_t object, const char *wmesg)
{
	ASSERT_LWKT_TOKEN_HELD(&vmobj_token);
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
 * Must be called with the vmobj_token held.
 */
void
vm_object_dead_wakeup(vm_object_t object)
{
	ASSERT_LWKT_TOKEN_HELD(&vmobj_token);
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
	int curgeneration;

	lwkt_gettoken(&vm_token);
	if (object->type != OBJT_VNODE ||
	    (object->flags & OBJ_MIGHTBEDIRTY) == 0) {
		lwkt_reltoken(&vm_token);
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
	crit_enter();
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
		curgeneration = object->generation;
		vm_page_rb_tree_RB_SCAN(&object->rb_memq, rb_vm_page_scancmp,
					vm_object_page_clean_pass2, &info);
	} while (info.error || curgeneration != object->generation);

	vm_object_clear_flag(object, OBJ_CLEANING);
	crit_exit();
	lwkt_reltoken(&vm_token);
}

/*
 * The caller must hold vm_token.
 */
static 
int
vm_object_page_clean_pass1(struct vm_page *p, void *data)
{
	struct rb_vm_page_scan_info *info = data;

	vm_page_flag_set(p, PG_CLEANCHK);
	if ((info->limit & OBJPC_NOSYNC) && (p->flags & PG_NOSYNC))
		info->error = 1;
	else
		vm_page_protect(p, VM_PROT_READ);	/* must not block */
	return(0);
}

/*
 * The caller must hold vm_token.
 */
static 
int
vm_object_page_clean_pass2(struct vm_page *p, void *data)
{
	struct rb_vm_page_scan_info *info = data;
	int n;

	/*
	 * Do not mess with pages that were inserted after we started
	 * the cleaning pass.
	 */
	if ((p->flags & PG_CLEANCHK) == 0)
		return(0);

	/*
	 * Before wasting time traversing the pmaps, check for trivial
	 * cases where the page cannot be dirty.
	 */
	if (p->valid == 0 || (p->queue - p->pc) == PQ_CACHE) {
		KKASSERT((p->dirty & p->valid) == 0);
		return(0);
	}

	/*
	 * Check whether the page is dirty or not.  The page has been set
	 * to be read-only so the check will not race a user dirtying the
	 * page.
	 */
	vm_page_test_dirty(p);
	if ((p->dirty & p->valid) == 0) {
		vm_page_flag_clear(p, PG_CLEANCHK);
		return(0);
	}

	/*
	 * If we have been asked to skip nosync pages and this is a
	 * nosync page, skip it.  Note that the object flags were
	 * not cleared in this case (because pass1 will have returned an
	 * error), so we do not have to set them.
	 */
	if ((info->limit & OBJPC_NOSYNC) && (p->flags & PG_NOSYNC)) {
		vm_page_flag_clear(p, PG_CLEANCHK);
		return(0);
	}

	/*
	 * Flush as many pages as we can.  PG_CLEANCHK will be cleared on
	 * the pages that get successfully flushed.  Set info->error if
	 * we raced an object modification.
	 */
	n = vm_object_page_collect_flush(info->object, p, info->pagerflags);
	if (n == 0)
		info->error = 1;
	return(0);
}

/*
 * Collect the specified page and nearby pages and flush them out.
 * The number of pages flushed is returned.
 *
 * The caller must hold vm_token.
 */
static int
vm_object_page_collect_flush(vm_object_t object, vm_page_t p, int pagerflags)
{
	int runlen;
	int maxf;
	int chkb;
	int maxb;
	int i;
	int curgeneration;
	vm_pindex_t pi;
	vm_page_t maf[vm_pageout_page_count];
	vm_page_t mab[vm_pageout_page_count];
	vm_page_t ma[vm_pageout_page_count];

	curgeneration = object->generation;

	pi = p->pindex;
	while (vm_page_sleep_busy(p, TRUE, "vpcwai")) {
		if (object->generation != curgeneration) {
			return(0);
		}
	}
	KKASSERT(p->object == object && p->pindex == pi);

	maxf = 0;
	for(i = 1; i < vm_pageout_page_count; i++) {
		vm_page_t tp;

		if ((tp = vm_page_lookup(object, pi + i)) != NULL) {
			if ((tp->flags & PG_BUSY) ||
				((pagerflags & VM_PAGER_IGNORE_CLEANCHK) == 0 && 
				 (tp->flags & PG_CLEANCHK) == 0) ||
				(tp->busy != 0))
				break;
			if((tp->queue - tp->pc) == PQ_CACHE) {
				vm_page_flag_clear(tp, PG_CLEANCHK);
				break;
			}
			vm_page_test_dirty(tp);
			if ((tp->dirty & tp->valid) == 0) {
				vm_page_flag_clear(tp, PG_CLEANCHK);
				break;
			}
			maf[ i - 1 ] = tp;
			maxf++;
			continue;
		}
		break;
	}

	maxb = 0;
	chkb = vm_pageout_page_count -  maxf;
	if (chkb) {
		for(i = 1; i < chkb;i++) {
			vm_page_t tp;

			if ((tp = vm_page_lookup(object, pi - i)) != NULL) {
				if ((tp->flags & PG_BUSY) ||
					((pagerflags & VM_PAGER_IGNORE_CLEANCHK) == 0 && 
					 (tp->flags & PG_CLEANCHK) == 0) ||
					(tp->busy != 0))
					break;
				if((tp->queue - tp->pc) == PQ_CACHE) {
					vm_page_flag_clear(tp, PG_CLEANCHK);
					break;
				}
				vm_page_test_dirty(tp);
				if ((tp->dirty & tp->valid) == 0) {
					vm_page_flag_clear(tp, PG_CLEANCHK);
					break;
				}
				mab[ i - 1 ] = tp;
				maxb++;
				continue;
			}
			break;
		}
	}

	for(i = 0; i < maxb; i++) {
		int index = (maxb - i) - 1;
		ma[index] = mab[i];
		vm_page_flag_clear(ma[index], PG_CLEANCHK);
	}
	vm_page_flag_clear(p, PG_CLEANCHK);
	ma[maxb] = p;
	for(i = 0; i < maxf; i++) {
		int index = (maxb + i) + 1;
		ma[index] = maf[i];
		vm_page_flag_clear(ma[index], PG_CLEANCHK);
	}
	runlen = maxb + maxf + 1;

	vm_pageout_flush(ma, runlen, pagerflags);
	for (i = 0; i < runlen; i++) {
		if (ma[i]->valid & ma[i]->dirty) {
			vm_page_protect(ma[i], VM_PROT_READ);
			vm_page_flag_set(ma[i], PG_CLEANCHK);

			/*
			 * maxf will end up being the actual number of pages
			 * we wrote out contiguously, non-inclusive of the
			 * first page.  We do not count look-behind pages.
			 */
			if (i >= maxb + 1 && (maxf > i - maxb - 1))
				maxf = i - maxb - 1;
		}
	}
	return(maxf + 1);
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

	/*
	 * spl protection needed to prevent races between the lookup,
	 * an interrupt unbusy/free, and our protect call.
	 */
	crit_enter();
	lwkt_gettoken(&vm_token);
	for (idx = start; idx < end; idx++) {
		p = vm_page_lookup(object, idx);
		if (p == NULL)
			continue;
		vm_page_protect(p, VM_PROT_READ);
	}
	lwkt_reltoken(&vm_token);
	crit_exit();
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

	crit_enter();
	lwkt_gettoken(&vm_token);
	vm_page_rb_tree_RB_SCAN(&object->rb_memq, rb_vm_page_scancmp,
				vm_object_pmap_remove_callback, &info);
	if (start == 0 && end == object->size)
		vm_object_clear_flag(object, OBJ_WRITEABLE);
	lwkt_reltoken(&vm_token);
	crit_exit();
}

/*
 * The caller must hold vm_token.
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
	vm_page_t m;

	if (object == NULL)
		return;

	end = pindex + count;

	lwkt_gettoken(&vm_token);

	/*
	 * Locate and adjust resident pages
	 */
	for (; pindex < end; pindex += 1) {
relookup:
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

		/*
		 * spl protection is required to avoid a race between the
		 * lookup, an interrupt unbusy/free, and our busy check.
		 */

		crit_enter();
		m = vm_page_lookup(tobject, tpindex);

		if (m == NULL) {
			/*
			 * There may be swap even if there is no backing page
			 */
			if (advise == MADV_FREE && tobject->type == OBJT_SWAP)
				swap_pager_freespace(tobject, tpindex, 1);

			/*
			 * next object
			 */
			crit_exit();
			if (tobject->backing_object == NULL)
				continue;
			tpindex += OFF_TO_IDX(tobject->backing_object_offset);
			tobject = tobject->backing_object;
			goto shadowlookup;
		}

		/*
		 * If the page is busy or not in a normal active state,
		 * we skip it.  If the page is not managed there are no
		 * page queues to mess with.  Things can break if we mess
		 * with pages in any of the below states.
		 */
		if (
		    m->hold_count ||
		    m->wire_count ||
		    (m->flags & PG_UNMANAGED) ||
		    m->valid != VM_PAGE_BITS_ALL
		) {
			crit_exit();
			continue;
		}

 		if (vm_page_sleep_busy(m, TRUE, "madvpo")) {
			crit_exit();
  			goto relookup;
		}
		vm_page_busy(m);
		crit_exit();

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
	lwkt_reltoken(&vm_token);
}

/*
 * Create a new object which is backed by the specified existing object
 * range.  The source object reference is deallocated.
 *
 * The new object and offset into that object are returned in the source
 * parameters.
 *
 * No other requirements.
 */
void
vm_object_shadow(vm_object_t *object, vm_ooffset_t *offset, vm_size_t length)
{
	vm_object_t source;
	vm_object_t result;

	source = *object;

	/*
	 * Don't create the new object if the old object isn't shared.
	 */
	lwkt_gettoken(&vm_token);

	if (source != NULL &&
	    source->ref_count == 1 &&
	    source->handle == NULL &&
	    (source->type == OBJT_DEFAULT ||
	     source->type == OBJT_SWAP)) {
		lwkt_reltoken(&vm_token);
		return;
	}

	/*
	 * Allocate a new object with the given length
	 */

	if ((result = vm_object_allocate(OBJT_DEFAULT, length)) == NULL)
		panic("vm_object_shadow: no object for shadowing");

	/*
	 * The new object shadows the source object, adding a reference to it.
	 * Our caller changes his reference to point to the new object,
	 * removing a reference to the source object.  Net result: no change
	 * of reference count.
	 *
	 * Try to optimize the result object's page color when shadowing
	 * in order to maintain page coloring consistency in the combined 
	 * shadowed object.
	 */
	result->backing_object = source;
	if (source) {
		LIST_INSERT_HEAD(&source->shadow_head, result, shadow_list);
		source->shadow_count++;
		source->generation++;
		result->pg_color = (source->pg_color + OFF_TO_IDX(*offset)) & PQ_L2_MASK;
	}

	/*
	 * Store the offset into the source object, and fix up the offset into
	 * the new object.
	 */
	result->backing_object_offset = *offset;
	lwkt_reltoken(&vm_token);

	/*
	 * Return the new things
	 */
	*offset = 0;
	*object = result;
}

#define	OBSC_TEST_ALL_SHADOWED	0x0001
#define	OBSC_COLLAPSE_NOWAIT	0x0002
#define	OBSC_COLLAPSE_WAIT	0x0004

static int vm_object_backing_scan_callback(vm_page_t p, void *data);

/*
 * The caller must hold vm_token.
 */
static __inline int
vm_object_backing_scan(vm_object_t object, int op)
{
	struct rb_vm_page_scan_info info;
	vm_object_t backing_object;

	crit_enter();

	backing_object = object->backing_object;
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
		if (backing_object->type != OBJT_DEFAULT) {
			crit_exit();
			return(0);
		}
	}
	if (op & OBSC_COLLAPSE_WAIT) {
		KKASSERT((backing_object->flags & OBJ_DEAD) == 0);
		vm_object_set_flag(backing_object, OBJ_DEAD);
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
	crit_exit();
	return(info.error);
}

/*
 * The caller must hold vm_token.
 */
static int
vm_object_backing_scan_callback(vm_page_t p, void *data)
{
	struct rb_vm_page_scan_info *info = data;
	vm_object_t backing_object;
	vm_object_t object;
	vm_pindex_t new_pindex;
	vm_pindex_t backing_offset_index;
	int op;

	new_pindex = p->pindex - info->backing_offset_index;
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
		if (
		    p->pindex < backing_offset_index ||
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
	 * Check for busy page
	 */

	if (op & (OBSC_COLLAPSE_WAIT | OBSC_COLLAPSE_NOWAIT)) {
		vm_page_t pp;

		if (op & OBSC_COLLAPSE_NOWAIT) {
			if (
			    (p->flags & PG_BUSY) ||
			    !p->valid || 
			    p->hold_count || 
			    p->wire_count ||
			    p->busy
			) {
				return(0);
			}
		} else if (op & OBSC_COLLAPSE_WAIT) {
			if (vm_page_sleep_busy(p, TRUE, "vmocol")) {
				/*
				 * If we slept, anything could have
				 * happened.   Ask that the scan be restarted.
				 *
				 * Since the object is marked dead, the
				 * backing offset should not have changed.  
				 */
				info->error = -1;
				return(-1);
			}
		}

		/* 
		 * Busy the page
		 */
		vm_page_busy(p);

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
		/* page automatically made dirty by rename */
	}
	return(0);
}

/*
 * This version of collapse allows the operation to occur earlier and
 * when paging_in_progress is true for an object...  This is not a complete
 * operation, but should plug 99.9% of the rest of the leaks.
 *
 * The caller must hold vm_token and vmobj_token.
 * (only called from vm_object_collapse)
 */
static void
vm_object_qcollapse(vm_object_t object)
{
	vm_object_t backing_object = object->backing_object;

	if (backing_object->ref_count != 1)
		return;

	backing_object->ref_count += 2;

	vm_object_backing_scan(object, OBSC_COLLAPSE_NOWAIT);

	backing_object->ref_count -= 2;
}

/*
 * Collapse an object with the object backing it.  Pages in the backing
 * object are moved into the parent, and the backing object is deallocated.
 */
void
vm_object_collapse(vm_object_t object)
{
	ASSERT_LWKT_TOKEN_HELD(&vm_token);
	ASSERT_LWKT_TOKEN_HELD(&vmobj_token);

	while (TRUE) {
		vm_object_t backing_object;

		/*
		 * Verify that the conditions are right for collapse:
		 *
		 * The object exists and the backing object exists.
		 */
		if (object == NULL)
			break;

		if ((backing_object = object->backing_object) == NULL)
			break;

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

		if (
		    object->paging_in_progress != 0 ||
		    backing_object->paging_in_progress != 0
		) {
			vm_object_qcollapse(object);
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
			vm_object_backing_scan(object, OBSC_COLLAPSE_WAIT);

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
				swap_pager_copy(
				    backing_object,
				    object,
				    OFF_TO_IDX(object->backing_object_offset), TRUE);
				vm_object_pip_wakeup(object);

				vm_object_pip_wakeup(backing_object);
			}
			/*
			 * Object now shadows whatever backing_object did.
			 * Note that the reference to 
			 * backing_object->backing_object moves from within 
			 * backing_object to within object.
			 */

			LIST_REMOVE(object, shadow_list);
			object->backing_object->shadow_count--;
			object->backing_object->generation++;
			if (backing_object->backing_object) {
				LIST_REMOVE(backing_object, shadow_list);
				backing_object->backing_object->shadow_count--;
				backing_object->backing_object->generation++;
			}
			object->backing_object = backing_object->backing_object;
			if (object->backing_object) {
				LIST_INSERT_HEAD(
				    &object->backing_object->shadow_head,
				    object, 
				    shadow_list
				);
				object->backing_object->shadow_count++;
				object->backing_object->generation++;
			}

			object->backing_object_offset +=
			    backing_object->backing_object_offset;

			/*
			 * Discard backing_object.
			 *
			 * Since the backing object has no pages, no pager left,
			 * and no object references within it, all that is
			 * necessary is to dispose of it.
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
			 * Wait for hold count to hit zero
			 */
			vm_object_hold_wait(backing_object);

			/* (we are holding vmobj_token) */
			TAILQ_REMOVE(&vm_object_list, backing_object,
				     object_list);
			vm_object_count--;

			zfree(obj_zone, backing_object);

			object_collapses++;
		} else {
			vm_object_t new_backing_object;

			/*
			 * If we do not entirely shadow the backing object,
			 * there is nothing we can do so we give up.
			 */

			if (vm_object_backing_scan(object, OBSC_TEST_ALL_SHADOWED) == 0) {
				break;
			}

			/*
			 * Make the parent shadow the next object in the
			 * chain.  Deallocating backing_object will not remove
			 * it, since its reference count is at least 2.
			 */

			LIST_REMOVE(object, shadow_list);
			backing_object->shadow_count--;
			backing_object->generation++;

			new_backing_object = backing_object->backing_object;
			if ((object->backing_object = new_backing_object) != NULL) {
				vm_object_reference(new_backing_object);
				LIST_INSERT_HEAD(
				    &new_backing_object->shadow_head,
				    object,
				    shadow_list
				);
				new_backing_object->shadow_count++;
				new_backing_object->generation++;
				object->backing_object_offset +=
					backing_object->backing_object_offset;
			}

			/*
			 * Drop the reference count on backing_object. Since
			 * its ref_count was at least 2, it will not vanish;
			 * so we don't need to call vm_object_deallocate, but
			 * we do anyway.
			 */
			vm_object_deallocate_locked(backing_object);
			object_bypasses++;
		}

		/*
		 * Try again with this object's new backing object.
		 */
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
	lwkt_gettoken(&vm_token);
	if (object == NULL ||
	    (object->resident_page_count == 0 && object->swblock_count == 0)) {
		lwkt_reltoken(&vm_token);
		return;
	}
	KASSERT(object->type != OBJT_PHYS, 
		("attempt to remove pages from a physical object"));

	/*
	 * Indicate that paging is occuring on the object
	 */
	crit_enter();
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
	crit_exit();
	lwkt_reltoken(&vm_token);
}

/*
 * The caller must hold vm_token.
 */
static int
vm_object_page_remove_callback(vm_page_t p, void *data)
{
	struct rb_vm_page_scan_info *info = data;

	/*
	 * Wired pages cannot be destroyed, but they can be invalidated
	 * and we do so if clean_only (limit) is not set.
	 *
	 * WARNING!  The page may be wired due to being part of a buffer
	 *	     cache buffer, and the buffer might be marked B_CACHE.
	 *	     This is fine as part of a truncation but VFSs must be
	 *	     sure to fix the buffer up when re-extending the file.
	 */
	if (p->wire_count != 0) {
		vm_page_protect(p, VM_PROT_NONE);
		if (info->limit == 0)
			p->valid = 0;
		return(0);
	}

	/*
	 * The busy flags are only cleared at
	 * interrupt -- minimize the spl transitions
	 */

	if (vm_page_sleep_busy(p, TRUE, "vmopar")) {
		info->error = 1;
		return(0);
	}

	/*
	 * limit is our clean_only flag.  If set and the page is dirty, do
	 * not free it.  If set and the page is being held by someone, do
	 * not free it.
	 */
	if (info->limit && p->valid) {
		vm_page_test_dirty(p);
		if (p->valid & p->dirty)
			return(0);
		if (p->hold_count)
			return(0);
	}

	/*
	 * Destroy the page
	 */
	vm_page_busy(p);
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
 * The object must not be locked.
 * The caller must hold vm_token and vmobj_token.
 */
boolean_t
vm_object_coalesce(vm_object_t prev_object, vm_pindex_t prev_pindex,
		   vm_size_t prev_size, vm_size_t next_size)
{
	vm_pindex_t next_pindex;

	ASSERT_LWKT_TOKEN_HELD(&vm_token);
	ASSERT_LWKT_TOKEN_HELD(&vmobj_token);

	if (prev_object == NULL) {
		return (TRUE);
	}

	if (prev_object->type != OBJT_DEFAULT &&
	    prev_object->type != OBJT_SWAP) {
		return (FALSE);
	}

	/*
	 * Try to collapse the object first
	 */
	vm_object_collapse(prev_object);

	/*
	 * Can't coalesce if: . more than one reference . paged out . shadows
	 * another object . has a copy elsewhere (any of which mean that the
	 * pages not mapped to prev_entry may be in use anyway)
	 */

	if (prev_object->backing_object != NULL) {
		return (FALSE);
	}

	prev_size >>= PAGE_SHIFT;
	next_size >>= PAGE_SHIFT;
	next_pindex = prev_pindex + prev_size;

	if ((prev_object->ref_count > 1) &&
	    (prev_object->size != next_pindex)) {
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

	return (TRUE);
}

/*
 * Make the object writable and flag is being possibly dirty.
 *
 * No requirements.
 */
void
vm_object_set_writeable_dirty(vm_object_t object)
{
	struct vnode *vp;

	lwkt_gettoken(&vm_token);
	vm_object_set_flag(object, OBJ_WRITEABLE|OBJ_MIGHTBEDIRTY);
	if (object->type == OBJT_VNODE &&
	    (vp = (struct vnode *)object->handle) != NULL) {
		if ((vp->v_flag & VOBJDIRTY) == 0) {
			vsetflags(vp, VOBJDIRTY);
		}
	}
	lwkt_reltoken(&vm_token);
}

static void
vm_object_lock_init(vm_object_t obj)
{
}

void
vm_object_lock(vm_object_t obj)
{
	lwkt_getpooltoken(obj);
}

void
vm_object_unlock(vm_object_t obj)
{
	lwkt_relpooltoken(obj);
}

void
vm_object_hold(vm_object_t obj)
{
	vm_object_lock(obj);

	refcount_acquire(&obj->hold_count);
}

void
vm_object_drop(vm_object_t obj)
{
	int rc;

	rc = refcount_release(&obj->hold_count);
	vm_object_unlock(obj);

	if (rc) 
		vm_object_hold_wake(obj);
}

static void
vm_object_hold_wake(vm_object_t obj)
{
	wakeup(obj);
}

static void
vm_object_hold_wait(vm_object_t obj)
{
	vm_object_lock(obj);

	while (obj->hold_count)
		tsleep(obj, 0, "vmobjhld", 0);

	vm_object_unlock(obj);
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
 * The caller must hold vm_token.
 */
static int
_vm_object_in_map(vm_map_t map, vm_object_t object, vm_map_entry_t entry)
{
	vm_map_t tmpm;
	vm_map_entry_t tmpe;
	vm_object_t obj;
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
			if (obj == object)
				return 1;
			obj = obj->backing_object;
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
