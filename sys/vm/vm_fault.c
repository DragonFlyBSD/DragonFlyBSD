/*
 * (MPSAFE)
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 * Copyright (c) 1994 David Greenman
 * All rights reserved.
 *
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
 *	from: @(#)vm_fault.c	8.4 (Berkeley) 1/12/94
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
 * $FreeBSD: src/sys/vm/vm_fault.c,v 1.108.2.8 2002/02/26 05:49:27 silby Exp $
 * $DragonFly: src/sys/vm/vm_fault.c,v 1.47 2008/07/01 02:02:56 dillon Exp $
 */

/*
 *	Page fault handling module.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/resourcevar.h>
#include <sys/vmmeter.h>
#include <sys/vkernel.h>
#include <sys/lock.h>
#include <sys/sysctl.h>

#include <cpu/lwbuf.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_kern.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
#include <vm/vm_extern.h>

#include <sys/thread2.h>
#include <vm/vm_page2.h>

struct faultstate {
	vm_page_t m;
	vm_object_t object;
	vm_pindex_t pindex;
	vm_prot_t prot;
	vm_page_t first_m;
	vm_object_t first_object;
	vm_prot_t first_prot;
	vm_map_t map;
	vm_map_entry_t entry;
	int lookup_still_valid;
	int hardfault;
	int fault_flags;
	int map_generation;
	int shared;
	int first_shared;
	boolean_t wired;
	struct vnode *vp;
};

static int debug_cluster = 0;
SYSCTL_INT(_vm, OID_AUTO, debug_cluster, CTLFLAG_RW, &debug_cluster, 0, "");
int vm_shared_fault = 1;
TUNABLE_INT("vm.shared_fault", &vm_shared_fault);
SYSCTL_INT(_vm, OID_AUTO, shared_fault, CTLFLAG_RW, &vm_shared_fault, 0,
	   "Allow shared token on vm_object");
static long vm_shared_hit = 0;
SYSCTL_LONG(_vm, OID_AUTO, shared_hit, CTLFLAG_RW, &vm_shared_hit, 0,
	   "Successful shared faults");
static long vm_shared_count = 0;
SYSCTL_LONG(_vm, OID_AUTO, shared_count, CTLFLAG_RW, &vm_shared_count, 0,
	   "Shared fault attempts");
static long vm_shared_miss = 0;
SYSCTL_LONG(_vm, OID_AUTO, shared_miss, CTLFLAG_RW, &vm_shared_miss, 0,
	   "Unsuccessful shared faults");

static int vm_fault_object(struct faultstate *, vm_pindex_t, vm_prot_t, int);
static int vm_fault_vpagetable(struct faultstate *, vm_pindex_t *,
			vpte_t, int, int);
#if 0
static int vm_fault_additional_pages (vm_page_t, int, int, vm_page_t *, int *);
#endif
static void vm_set_nosync(vm_page_t m, vm_map_entry_t entry);
static void vm_prefault(pmap_t pmap, vm_offset_t addra,
			vm_map_entry_t entry, int prot, int fault_flags);
static void vm_prefault_quick(pmap_t pmap, vm_offset_t addra,
			vm_map_entry_t entry, int prot, int fault_flags);

static __inline void
release_page(struct faultstate *fs)
{
	vm_page_deactivate(fs->m);
	vm_page_wakeup(fs->m);
	fs->m = NULL;
}

/*
 * NOTE: Once unlocked any cached fs->entry becomes invalid, any reuse
 *	 requires relocking and then checking the timestamp.
 *
 * NOTE: vm_map_lock_read() does not bump fs->map->timestamp so we do
 *	 not have to update fs->map_generation here.
 *
 * NOTE: This function can fail due to a deadlock against the caller's
 *	 holding of a vm_page BUSY.
 */
static __inline int
relock_map(struct faultstate *fs)
{
	int error;

	if (fs->lookup_still_valid == FALSE && fs->map) {
		error = vm_map_lock_read_to(fs->map);
		if (error == 0)
			fs->lookup_still_valid = TRUE;
	} else {
		error = 0;
	}
	return error;
}

static __inline void
unlock_map(struct faultstate *fs)
{
	if (fs->lookup_still_valid && fs->map) {
		vm_map_lookup_done(fs->map, fs->entry, 0);
		fs->lookup_still_valid = FALSE;
	}
}

/*
 * Clean up after a successful call to vm_fault_object() so another call
 * to vm_fault_object() can be made.
 */
static void
_cleanup_successful_fault(struct faultstate *fs, int relock)
{
	/*
	 * We allocated a junk page for a COW operation that did
	 * not occur, the page must be freed.
	 */
	if (fs->object != fs->first_object) {
		KKASSERT(fs->first_shared == 0);
		vm_page_free(fs->first_m);
		vm_object_pip_wakeup(fs->object);
		fs->first_m = NULL;
	}

	/*
	 * Reset fs->object.
	 */
	fs->object = fs->first_object;
	if (relock && fs->lookup_still_valid == FALSE) {
		if (fs->map)
			vm_map_lock_read(fs->map);
		fs->lookup_still_valid = TRUE;
	}
}

static void
_unlock_things(struct faultstate *fs, int dealloc)
{
	_cleanup_successful_fault(fs, 0);
	if (dealloc) {
		/*vm_object_deallocate(fs->first_object);*/
		/*fs->first_object = NULL; drop used later on */
	}
	unlock_map(fs);	
	if (fs->vp != NULL) { 
		vput(fs->vp);
		fs->vp = NULL;
	}
}

#define unlock_things(fs) _unlock_things(fs, 0)
#define unlock_and_deallocate(fs) _unlock_things(fs, 1)
#define cleanup_successful_fault(fs) _cleanup_successful_fault(fs, 1)

/*
 * TRYPAGER 
 *
 * Determine if the pager for the current object *might* contain the page.
 *
 * We only need to try the pager if this is not a default object (default
 * objects are zero-fill and have no real pager), and if we are not taking
 * a wiring fault or if the FS entry is wired.
 */
#define TRYPAGER(fs)	\
		(fs->object->type != OBJT_DEFAULT && \
		(((fs->fault_flags & VM_FAULT_WIRE_MASK) == 0) || fs->wired))

/*
 * vm_fault:
 *
 * Handle a page fault occuring at the given address, requiring the given
 * permissions, in the map specified.  If successful, the page is inserted
 * into the associated physical map.
 *
 * NOTE: The given address should be truncated to the proper page address.
 *
 * KERN_SUCCESS is returned if the page fault is handled; otherwise,
 * a standard error specifying why the fault is fatal is returned.
 *
 * The map in question must be referenced, and remains so.
 * The caller may hold no locks.
 * No other requirements.
 */
int
vm_fault(vm_map_t map, vm_offset_t vaddr, vm_prot_t fault_type, int fault_flags)
{
	int result;
	vm_pindex_t first_pindex;
	struct faultstate fs;
	struct lwp *lp;
	int growstack;
	int retry = 0;

	vm_page_pcpu_cache();
	fs.hardfault = 0;
	fs.fault_flags = fault_flags;
	fs.vp = NULL;
	fs.shared = vm_shared_fault;
	fs.first_shared = vm_shared_fault;
	growstack = 1;
	if (vm_shared_fault)
		++vm_shared_count;

	/*
	 * vm_map interactions
	 */
	if ((lp = curthread->td_lwp) != NULL)
		lp->lwp_flags |= LWP_PAGING;
	lwkt_gettoken(&map->token);

RetryFault:
	/*
	 * Find the vm_map_entry representing the backing store and resolve
	 * the top level object and page index.  This may have the side
	 * effect of executing a copy-on-write on the map entry and/or
	 * creating a shadow object, but will not COW any actual VM pages.
	 *
	 * On success fs.map is left read-locked and various other fields 
	 * are initialized but not otherwise referenced or locked.
	 *
	 * NOTE!  vm_map_lookup will try to upgrade the fault_type to
	 * VM_FAULT_WRITE if the map entry is a virtual page table and also
	 * writable, so we can set the 'A'accessed bit in the virtual page
	 * table entry.
	 */
	fs.map = map;
	result = vm_map_lookup(&fs.map, vaddr, fault_type,
			       &fs.entry, &fs.first_object,
			       &first_pindex, &fs.first_prot, &fs.wired);

	/*
	 * If the lookup failed or the map protections are incompatible,
	 * the fault generally fails.  However, if the caller is trying
	 * to do a user wiring we have more work to do.
	 */
	if (result != KERN_SUCCESS) {
		if (result != KERN_PROTECTION_FAILURE ||
		    (fs.fault_flags & VM_FAULT_WIRE_MASK) != VM_FAULT_USER_WIRE)
		{
			if (result == KERN_INVALID_ADDRESS && growstack &&
			    map != &kernel_map && curproc != NULL) {
				result = vm_map_growstack(curproc, vaddr);
				if (result == KERN_SUCCESS) {
					growstack = 0;
					++retry;
					goto RetryFault;
				}
				result = KERN_FAILURE;
			}
			goto done;
		}

		/*
   		 * If we are user-wiring a r/w segment, and it is COW, then
   		 * we need to do the COW operation.  Note that we don't
		 * currently COW RO sections now, because it is NOT desirable
   		 * to COW .text.  We simply keep .text from ever being COW'ed
   		 * and take the heat that one cannot debug wired .text sections.
   		 */
		result = vm_map_lookup(&fs.map, vaddr,
				       VM_PROT_READ|VM_PROT_WRITE|
				        VM_PROT_OVERRIDE_WRITE,
				       &fs.entry, &fs.first_object,
				       &first_pindex, &fs.first_prot,
				       &fs.wired);
		if (result != KERN_SUCCESS) {
			result = KERN_FAILURE;
			goto done;
		}

		/*
		 * If we don't COW now, on a user wire, the user will never
		 * be able to write to the mapping.  If we don't make this
		 * restriction, the bookkeeping would be nearly impossible.
		 *
		 * XXX We have a shared lock, this will have a MP race but
		 * I don't see how it can hurt anything.
		 */
		if ((fs.entry->protection & VM_PROT_WRITE) == 0)
			fs.entry->max_protection &= ~VM_PROT_WRITE;
	}

	/*
	 * fs.map is read-locked
	 *
	 * Misc checks.  Save the map generation number to detect races.
	 */
	fs.map_generation = fs.map->timestamp;
	fs.lookup_still_valid = TRUE;
	fs.first_m = NULL;
	fs.object = fs.first_object;	/* so unlock_and_deallocate works */

	if (fs.entry->eflags & (MAP_ENTRY_NOFAULT | MAP_ENTRY_KSTACK)) {
		if (fs.entry->eflags & MAP_ENTRY_NOFAULT) {
			panic("vm_fault: fault on nofault entry, addr: %p",
			      (void *)vaddr);
		}
		if ((fs.entry->eflags & MAP_ENTRY_KSTACK) &&
		    vaddr >= fs.entry->start &&
		    vaddr < fs.entry->start + PAGE_SIZE) {
			panic("vm_fault: fault on stack guard, addr: %p",
			      (void *)vaddr);
		}
	}

	/*
	 * A system map entry may return a NULL object.  No object means
	 * no pager means an unrecoverable kernel fault.
	 */
	if (fs.first_object == NULL) {
		panic("vm_fault: unrecoverable fault at %p in entry %p",
			(void *)vaddr, fs.entry);
	}

	/*
	 * Fail here if not a trivial anonymous page fault and TDF_NOFAULT
	 * is set.
	 */
	if ((curthread->td_flags & TDF_NOFAULT) &&
	    (retry ||
	     fs.first_object->type == OBJT_VNODE ||
	     fs.first_object->backing_object)) {
		result = KERN_FAILURE;
		unlock_things(&fs);
		goto done2;
	}

	/*
	 * If the entry is wired we cannot change the page protection.
	 */
	if (fs.wired)
		fault_type = fs.first_prot;

	/*
	 * We generally want to avoid unnecessary exclusive modes on backing
	 * and terminal objects because this can seriously interfere with
	 * heavily fork()'d processes (particularly /bin/sh scripts).
	 *
	 * However, we also want to avoid unnecessary retries due to needed
	 * shared->exclusive promotion for common faults.  Exclusive mode is
	 * always needed if any page insertion, rename, or free occurs in an
	 * object (and also indirectly if any I/O is done).
	 *
	 * The main issue here is going to be fs.first_shared.  If the
	 * first_object has a backing object which isn't shadowed and the
	 * process is single-threaded we might as well use an exclusive
	 * lock/chain right off the bat.
	 */
	if (fs.first_shared && fs.first_object->backing_object &&
	    LIST_EMPTY(&fs.first_object->shadow_head) &&
	    curthread->td_proc && curthread->td_proc->p_nthreads == 1) {
		fs.first_shared = 0;
	}


	/*
	 * Obtain a top-level object lock, shared or exclusive depending
	 * on fs.first_shared.  If a shared lock winds up being insufficient
	 * we will retry with an exclusive lock.
	 *
	 * The vnode pager lock is always shared.
	 */
	if (fs.first_shared)
		vm_object_hold_shared(fs.first_object);
	else
		vm_object_hold(fs.first_object);
	if (fs.vp == NULL)
		fs.vp = vnode_pager_lock(fs.first_object);

	/*
	 * The page we want is at (first_object, first_pindex), but if the
	 * vm_map_entry is VM_MAPTYPE_VPAGETABLE we have to traverse the
	 * page table to figure out the actual pindex.
	 *
	 * NOTE!  DEVELOPMENT IN PROGRESS, THIS IS AN INITIAL IMPLEMENTATION
	 * ONLY
	 */
	if (fs.entry->maptype == VM_MAPTYPE_VPAGETABLE) {
		result = vm_fault_vpagetable(&fs, &first_pindex,
					     fs.entry->aux.master_pde,
					     fault_type, 1);
		if (result == KERN_TRY_AGAIN) {
			vm_object_drop(fs.first_object);
			++retry;
			goto RetryFault;
		}
		if (result != KERN_SUCCESS)
			goto done;
	}

	/*
	 * Now we have the actual (object, pindex), fault in the page.  If
	 * vm_fault_object() fails it will unlock and deallocate the FS
	 * data.   If it succeeds everything remains locked and fs->object
	 * will have an additional PIP count if it is not equal to
	 * fs->first_object
	 *
	 * vm_fault_object will set fs->prot for the pmap operation.  It is
	 * allowed to set VM_PROT_WRITE if fault_type == VM_PROT_READ if the
	 * page can be safely written.  However, it will force a read-only
	 * mapping for a read fault if the memory is managed by a virtual
	 * page table.
	 *
	 * If the fault code uses the shared object lock shortcut
	 * we must not try to burst (we can't allocate VM pages).
	 */
	result = vm_fault_object(&fs, first_pindex, fault_type, 1);
	if (result == KERN_TRY_AGAIN) {
		vm_object_drop(fs.first_object);
		++retry;
		goto RetryFault;
	}
	if (result != KERN_SUCCESS)
		goto done;

	/*
	 * On success vm_fault_object() does not unlock or deallocate, and fs.m
	 * will contain a busied page.
	 *
	 * Enter the page into the pmap and do pmap-related adjustments.
	 */
	KKASSERT(fs.lookup_still_valid == TRUE);
	vm_page_flag_set(fs.m, PG_REFERENCED);
	pmap_enter(fs.map->pmap, vaddr, fs.m, fs.prot, fs.wired, fs.entry);
	mycpu->gd_cnt.v_vm_faults++;
	if (curthread->td_lwp)
		++curthread->td_lwp->lwp_ru.ru_minflt;

	/*KKASSERT(fs.m->queue == PQ_NONE); page-in op may deactivate page */
	KKASSERT(fs.m->flags & PG_BUSY);

	/*
	 * If the page is not wired down, then put it where the pageout daemon
	 * can find it.
	 */
	if (fs.fault_flags & VM_FAULT_WIRE_MASK) {
		if (fs.wired)
			vm_page_wire(fs.m);
		else
			vm_page_unwire(fs.m, 1);
	} else {
		vm_page_activate(fs.m);
	}
	vm_page_wakeup(fs.m);

	/*
	 * Burst in a few more pages if possible.  The fs.map should still
	 * be locked.  To avoid interlocking against a vnode->getblk
	 * operation we had to be sure to unbusy our primary vm_page above
	 * first.
	 *
	 * A normal burst can continue down backing store, only execute
	 * if we are holding an exclusive lock, otherwise the exclusive
	 * locks the burst code gets might cause excessive SMP collisions.
	 *
	 * A quick burst can be utilized when there is no backing object
	 * (i.e. a shared file mmap).
	 */
	if ((fault_flags & VM_FAULT_BURST) &&
	    (fs.fault_flags & VM_FAULT_WIRE_MASK) == 0 &&
	    fs.wired == 0) {
		if (fs.first_shared == 0 && fs.shared == 0) {
			vm_prefault(fs.map->pmap, vaddr,
				    fs.entry, fs.prot, fault_flags);
		} else {
			vm_prefault_quick(fs.map->pmap, vaddr,
					  fs.entry, fs.prot, fault_flags);
		}
	}

	/*
	 * Unlock everything, and return
	 */
	unlock_things(&fs);

	if (curthread->td_lwp) {
		if (fs.hardfault) {
			curthread->td_lwp->lwp_ru.ru_majflt++;
		} else {
			curthread->td_lwp->lwp_ru.ru_minflt++;
		}
	}

	/*vm_object_deallocate(fs.first_object);*/
	/*fs.m = NULL; */
	/*fs.first_object = NULL; must still drop later */

	result = KERN_SUCCESS;
done:
	if (fs.first_object)
		vm_object_drop(fs.first_object);
done2:
	lwkt_reltoken(&map->token);
	if (lp)
		lp->lwp_flags &= ~LWP_PAGING;
	if (vm_shared_fault && fs.shared == 0)
		++vm_shared_miss;
	return (result);
}

/*
 * Fault in the specified virtual address in the current process map, 
 * returning a held VM page or NULL.  See vm_fault_page() for more 
 * information.
 *
 * No requirements.
 */
vm_page_t
vm_fault_page_quick(vm_offset_t va, vm_prot_t fault_type, int *errorp)
{
	struct lwp *lp = curthread->td_lwp;
	vm_page_t m;

	m = vm_fault_page(&lp->lwp_vmspace->vm_map, va, 
			  fault_type, VM_FAULT_NORMAL, errorp);
	return(m);
}

/*
 * Fault in the specified virtual address in the specified map, doing all
 * necessary manipulation of the object store and all necessary I/O.  Return
 * a held VM page or NULL, and set *errorp.  The related pmap is not
 * updated.
 *
 * The returned page will be properly dirtied if VM_PROT_WRITE was specified,
 * and marked PG_REFERENCED as well.
 *
 * If the page cannot be faulted writable and VM_PROT_WRITE was specified, an
 * error will be returned.
 *
 * No requirements.
 */
vm_page_t
vm_fault_page(vm_map_t map, vm_offset_t vaddr, vm_prot_t fault_type,
	      int fault_flags, int *errorp)
{
	vm_pindex_t first_pindex;
	struct faultstate fs;
	int result;
	int retry = 0;
	vm_prot_t orig_fault_type = fault_type;

	fs.hardfault = 0;
	fs.fault_flags = fault_flags;
	KKASSERT((fault_flags & VM_FAULT_WIRE_MASK) == 0);

	/*
	 * Dive the pmap (concurrency possible).  If we find the
	 * appropriate page we can terminate early and quickly.
	 */
	fs.m = pmap_fault_page_quick(map->pmap, vaddr, fault_type);
	if (fs.m) {
		*errorp = 0;
		return(fs.m);
	}

	/*
	 * Otherwise take a concurrency hit and do a formal page
	 * fault.
	 */
	fs.shared = vm_shared_fault;
	fs.first_shared = vm_shared_fault;
	fs.vp = NULL;
	lwkt_gettoken(&map->token);

RetryFault:
	/*
	 * Find the vm_map_entry representing the backing store and resolve
	 * the top level object and page index.  This may have the side
	 * effect of executing a copy-on-write on the map entry and/or
	 * creating a shadow object, but will not COW any actual VM pages.
	 *
	 * On success fs.map is left read-locked and various other fields 
	 * are initialized but not otherwise referenced or locked.
	 *
	 * NOTE!  vm_map_lookup will upgrade the fault_type to VM_FAULT_WRITE
	 * if the map entry is a virtual page table and also writable,
	 * so we can set the 'A'accessed bit in the virtual page table entry.
	 */
	fs.map = map;
	result = vm_map_lookup(&fs.map, vaddr, fault_type,
			       &fs.entry, &fs.first_object,
			       &first_pindex, &fs.first_prot, &fs.wired);

	if (result != KERN_SUCCESS) {
		*errorp = result;
		fs.m = NULL;
		goto done;
	}

	/*
	 * fs.map is read-locked
	 *
	 * Misc checks.  Save the map generation number to detect races.
	 */
	fs.map_generation = fs.map->timestamp;
	fs.lookup_still_valid = TRUE;
	fs.first_m = NULL;
	fs.object = fs.first_object;	/* so unlock_and_deallocate works */

	if (fs.entry->eflags & MAP_ENTRY_NOFAULT) {
		panic("vm_fault: fault on nofault entry, addr: %lx",
		    (u_long)vaddr);
	}

	/*
	 * A system map entry may return a NULL object.  No object means
	 * no pager means an unrecoverable kernel fault.
	 */
	if (fs.first_object == NULL) {
		panic("vm_fault: unrecoverable fault at %p in entry %p",
			(void *)vaddr, fs.entry);
	}

	/*
	 * Fail here if not a trivial anonymous page fault and TDF_NOFAULT
	 * is set.
	 */
	if ((curthread->td_flags & TDF_NOFAULT) &&
	    (retry ||
	     fs.first_object->type == OBJT_VNODE ||
	     fs.first_object->backing_object)) {
		*errorp = KERN_FAILURE;
		unlock_things(&fs);
		goto done2;
	}

	/*
	 * If the entry is wired we cannot change the page protection.
	 */
	if (fs.wired)
		fault_type = fs.first_prot;

	/*
	 * Make a reference to this object to prevent its disposal while we
	 * are messing with it.  Once we have the reference, the map is free
	 * to be diddled.  Since objects reference their shadows (and copies),
	 * they will stay around as well.
	 *
	 * The reference should also prevent an unexpected collapse of the
	 * parent that might move pages from the current object into the
	 * parent unexpectedly, resulting in corruption.
	 *
	 * Bump the paging-in-progress count to prevent size changes (e.g.
	 * truncation operations) during I/O.  This must be done after
	 * obtaining the vnode lock in order to avoid possible deadlocks.
	 */
	if (fs.first_shared)
		vm_object_hold_shared(fs.first_object);
	else
		vm_object_hold(fs.first_object);
	if (fs.vp == NULL)
		fs.vp = vnode_pager_lock(fs.first_object);	/* shared */

	/*
	 * The page we want is at (first_object, first_pindex), but if the
	 * vm_map_entry is VM_MAPTYPE_VPAGETABLE we have to traverse the
	 * page table to figure out the actual pindex.
	 *
	 * NOTE!  DEVELOPMENT IN PROGRESS, THIS IS AN INITIAL IMPLEMENTATION
	 * ONLY
	 */
	if (fs.entry->maptype == VM_MAPTYPE_VPAGETABLE) {
		result = vm_fault_vpagetable(&fs, &first_pindex,
					     fs.entry->aux.master_pde,
					     fault_type, 1);
		if (result == KERN_TRY_AGAIN) {
			vm_object_drop(fs.first_object);
			++retry;
			goto RetryFault;
		}
		if (result != KERN_SUCCESS) {
			*errorp = result;
			fs.m = NULL;
			goto done;
		}
	}

	/*
	 * Now we have the actual (object, pindex), fault in the page.  If
	 * vm_fault_object() fails it will unlock and deallocate the FS
	 * data.   If it succeeds everything remains locked and fs->object
	 * will have an additinal PIP count if it is not equal to
	 * fs->first_object
	 */
	fs.m = NULL;
	result = vm_fault_object(&fs, first_pindex, fault_type, 1);

	if (result == KERN_TRY_AGAIN) {
		vm_object_drop(fs.first_object);
		++retry;
		goto RetryFault;
	}
	if (result != KERN_SUCCESS) {
		*errorp = result;
		fs.m = NULL;
		goto done;
	}

	if ((orig_fault_type & VM_PROT_WRITE) &&
	    (fs.prot & VM_PROT_WRITE) == 0) {
		*errorp = KERN_PROTECTION_FAILURE;
		unlock_and_deallocate(&fs);
		fs.m = NULL;
		goto done;
	}

	/*
	 * DO NOT UPDATE THE PMAP!!!  This function may be called for
	 * a pmap unrelated to the current process pmap, in which case
	 * the current cpu core will not be listed in the pmap's pm_active
	 * mask.  Thus invalidation interlocks will fail to work properly.
	 *
	 * (for example, 'ps' uses procfs to read program arguments from
	 * each process's stack).
	 *
	 * In addition to the above this function will be called to acquire
	 * a page that might already be faulted in, re-faulting it
	 * continuously is a waste of time.
	 *
	 * XXX could this have been the cause of our random seg-fault
	 *     issues?  procfs accesses user stacks.
	 */
	vm_page_flag_set(fs.m, PG_REFERENCED);
#if 0
	pmap_enter(fs.map->pmap, vaddr, fs.m, fs.prot, fs.wired, NULL);
	mycpu->gd_cnt.v_vm_faults++;
	if (curthread->td_lwp)
		++curthread->td_lwp->lwp_ru.ru_minflt;
#endif

	/*
	 * On success vm_fault_object() does not unlock or deallocate, and fs.m
	 * will contain a busied page.  So we must unlock here after having
	 * messed with the pmap.
	 */
	unlock_things(&fs);

	/*
	 * Return a held page.  We are not doing any pmap manipulation so do
	 * not set PG_MAPPED.  However, adjust the page flags according to
	 * the fault type because the caller may not use a managed pmapping
	 * (so we don't want to lose the fact that the page will be dirtied
	 * if a write fault was specified).
	 */
	vm_page_hold(fs.m);
	vm_page_activate(fs.m);
	if (fault_type & VM_PROT_WRITE)
		vm_page_dirty(fs.m);

	if (curthread->td_lwp) {
		if (fs.hardfault) {
			curthread->td_lwp->lwp_ru.ru_majflt++;
		} else {
			curthread->td_lwp->lwp_ru.ru_minflt++;
		}
	}

	/*
	 * Unlock everything, and return the held page.
	 */
	vm_page_wakeup(fs.m);
	/*vm_object_deallocate(fs.first_object);*/
	/*fs.first_object = NULL; */
	*errorp = 0;

done:
	if (fs.first_object)
		vm_object_drop(fs.first_object);
done2:
	lwkt_reltoken(&map->token);
	return(fs.m);
}

/*
 * Fault in the specified (object,offset), dirty the returned page as
 * needed.  If the requested fault_type cannot be done NULL and an
 * error is returned.
 *
 * A held (but not busied) page is returned.
 *
 * The passed in object must be held as specified by the shared
 * argument.
 */
vm_page_t
vm_fault_object_page(vm_object_t object, vm_ooffset_t offset,
		     vm_prot_t fault_type, int fault_flags,
		     int *sharedp, int *errorp)
{
	int result;
	vm_pindex_t first_pindex;
	struct faultstate fs;
	struct vm_map_entry entry;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	bzero(&entry, sizeof(entry));
	entry.object.vm_object = object;
	entry.maptype = VM_MAPTYPE_NORMAL;
	entry.protection = entry.max_protection = fault_type;

	fs.hardfault = 0;
	fs.fault_flags = fault_flags;
	fs.map = NULL;
	fs.shared = vm_shared_fault;
	fs.first_shared = *sharedp;
	fs.vp = NULL;
	KKASSERT((fault_flags & VM_FAULT_WIRE_MASK) == 0);

RetryFault:
	*sharedp = fs.first_shared;
	first_pindex = OFF_TO_IDX(offset);
	fs.first_object = object;
	fs.entry = &entry;
	fs.first_prot = fault_type;
	fs.wired = 0;
	/*fs.map_generation = 0; unused */

	/*
	 * Make a reference to this object to prevent its disposal while we
	 * are messing with it.  Once we have the reference, the map is free
	 * to be diddled.  Since objects reference their shadows (and copies),
	 * they will stay around as well.
	 *
	 * The reference should also prevent an unexpected collapse of the
	 * parent that might move pages from the current object into the
	 * parent unexpectedly, resulting in corruption.
	 *
	 * Bump the paging-in-progress count to prevent size changes (e.g.
	 * truncation operations) during I/O.  This must be done after
	 * obtaining the vnode lock in order to avoid possible deadlocks.
	 */
	if (fs.vp == NULL)
		fs.vp = vnode_pager_lock(fs.first_object);

	fs.lookup_still_valid = TRUE;
	fs.first_m = NULL;
	fs.object = fs.first_object;	/* so unlock_and_deallocate works */

#if 0
	/* XXX future - ability to operate on VM object using vpagetable */
	if (fs.entry->maptype == VM_MAPTYPE_VPAGETABLE) {
		result = vm_fault_vpagetable(&fs, &first_pindex,
					     fs.entry->aux.master_pde,
					     fault_type, 0);
		if (result == KERN_TRY_AGAIN) {
			if (fs.first_shared == 0 && *sharedp)
				vm_object_upgrade(object);
			goto RetryFault;
		}
		if (result != KERN_SUCCESS) {
			*errorp = result;
			return (NULL);
		}
	}
#endif

	/*
	 * Now we have the actual (object, pindex), fault in the page.  If
	 * vm_fault_object() fails it will unlock and deallocate the FS
	 * data.   If it succeeds everything remains locked and fs->object
	 * will have an additinal PIP count if it is not equal to
	 * fs->first_object
	 *
	 * On KERN_TRY_AGAIN vm_fault_object() leaves fs.first_object intact.
	 * We may have to upgrade its lock to handle the requested fault.
	 */
	result = vm_fault_object(&fs, first_pindex, fault_type, 0);

	if (result == KERN_TRY_AGAIN) {
		if (fs.first_shared == 0 && *sharedp)
			vm_object_upgrade(object);
		goto RetryFault;
	}
	if (result != KERN_SUCCESS) {
		*errorp = result;
		return(NULL);
	}

	if ((fault_type & VM_PROT_WRITE) && (fs.prot & VM_PROT_WRITE) == 0) {
		*errorp = KERN_PROTECTION_FAILURE;
		unlock_and_deallocate(&fs);
		return(NULL);
	}

	/*
	 * On success vm_fault_object() does not unlock or deallocate, so we
	 * do it here.  Note that the returned fs.m will be busied.
	 */
	unlock_things(&fs);

	/*
	 * Return a held page.  We are not doing any pmap manipulation so do
	 * not set PG_MAPPED.  However, adjust the page flags according to
	 * the fault type because the caller may not use a managed pmapping
	 * (so we don't want to lose the fact that the page will be dirtied
	 * if a write fault was specified).
	 */
	vm_page_hold(fs.m);
	vm_page_activate(fs.m);
	if ((fault_type & VM_PROT_WRITE) || (fault_flags & VM_FAULT_DIRTY))
		vm_page_dirty(fs.m);
	if (fault_flags & VM_FAULT_UNSWAP)
		swap_pager_unswapped(fs.m);

	/*
	 * Indicate that the page was accessed.
	 */
	vm_page_flag_set(fs.m, PG_REFERENCED);

	if (curthread->td_lwp) {
		if (fs.hardfault) {
			curthread->td_lwp->lwp_ru.ru_majflt++;
		} else {
			curthread->td_lwp->lwp_ru.ru_minflt++;
		}
	}

	/*
	 * Unlock everything, and return the held page.
	 */
	vm_page_wakeup(fs.m);
	/*vm_object_deallocate(fs.first_object);*/
	/*fs.first_object = NULL; */

	*errorp = 0;
	return(fs.m);
}

/*
 * Translate the virtual page number (first_pindex) that is relative
 * to the address space into a logical page number that is relative to the
 * backing object.  Use the virtual page table pointed to by (vpte).
 *
 * This implements an N-level page table.  Any level can terminate the
 * scan by setting VPTE_PS.   A linear mapping is accomplished by setting
 * VPTE_PS in the master page directory entry set via mcontrol(MADV_SETMAP).
 */
static
int
vm_fault_vpagetable(struct faultstate *fs, vm_pindex_t *pindex,
		    vpte_t vpte, int fault_type, int allow_nofault)
{
	struct lwbuf *lwb;
	struct lwbuf lwb_cache;
	int vshift = VPTE_FRAME_END - PAGE_SHIFT; /* index bits remaining */
	int result = KERN_SUCCESS;
	vpte_t *ptep;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(fs->first_object));
	for (;;) {
		/*
		 * We cannot proceed if the vpte is not valid, not readable
		 * for a read fault, or not writable for a write fault.
		 */
		if ((vpte & VPTE_V) == 0) {
			unlock_and_deallocate(fs);
			return (KERN_FAILURE);
		}
		if ((fault_type & VM_PROT_WRITE) && (vpte & VPTE_RW) == 0) {
			unlock_and_deallocate(fs);
			return (KERN_FAILURE);
		}
		if ((vpte & VPTE_PS) || vshift == 0)
			break;
		KKASSERT(vshift >= VPTE_PAGE_BITS);

		/*
		 * Get the page table page.  Nominally we only read the page
		 * table, but since we are actively setting VPTE_M and VPTE_A,
		 * tell vm_fault_object() that we are writing it. 
		 *
		 * There is currently no real need to optimize this.
		 */
		result = vm_fault_object(fs, (vpte & VPTE_FRAME) >> PAGE_SHIFT,
					 VM_PROT_READ|VM_PROT_WRITE,
					 allow_nofault);
		if (result != KERN_SUCCESS)
			return (result);

		/*
		 * Process the returned fs.m and look up the page table
		 * entry in the page table page.
		 */
		vshift -= VPTE_PAGE_BITS;
		lwb = lwbuf_alloc(fs->m, &lwb_cache);
		ptep = ((vpte_t *)lwbuf_kva(lwb) +
		        ((*pindex >> vshift) & VPTE_PAGE_MASK));
		vpte = *ptep;

		/*
		 * Page table write-back.  If the vpte is valid for the
		 * requested operation, do a write-back to the page table.
		 *
		 * XXX VPTE_M is not set properly for page directory pages.
		 * It doesn't get set in the page directory if the page table
		 * is modified during a read access.
		 */
		vm_page_activate(fs->m);
		if ((fault_type & VM_PROT_WRITE) && (vpte & VPTE_V) &&
		    (vpte & VPTE_RW)) {
			if ((vpte & (VPTE_M|VPTE_A)) != (VPTE_M|VPTE_A)) {
				atomic_set_long(ptep, VPTE_M | VPTE_A);
				vm_page_dirty(fs->m);
			}
		}
		if ((fault_type & VM_PROT_READ) && (vpte & VPTE_V)) {
			if ((vpte & VPTE_A) == 0) {
				atomic_set_long(ptep, VPTE_A);
				vm_page_dirty(fs->m);
			}
		}
		lwbuf_free(lwb);
		vm_page_flag_set(fs->m, PG_REFERENCED);
		vm_page_wakeup(fs->m);
		fs->m = NULL;
		cleanup_successful_fault(fs);
	}
	/*
	 * Combine remaining address bits with the vpte.
	 */
	/* JG how many bits from each? */
	*pindex = ((vpte & VPTE_FRAME) >> PAGE_SHIFT) +
		  (*pindex & ((1L << vshift) - 1));
	return (KERN_SUCCESS);
}


/*
 * This is the core of the vm_fault code.
 *
 * Do all operations required to fault-in (fs.first_object, pindex).  Run
 * through the shadow chain as necessary and do required COW or virtual
 * copy operations.  The caller has already fully resolved the vm_map_entry
 * and, if appropriate, has created a copy-on-write layer.  All we need to
 * do is iterate the object chain.
 *
 * On failure (fs) is unlocked and deallocated and the caller may return or
 * retry depending on the failure code.  On success (fs) is NOT unlocked or
 * deallocated, fs.m will contained a resolved, busied page, and fs.object
 * will have an additional PIP count if it is not equal to fs.first_object.
 *
 * If locks based on fs->first_shared or fs->shared are insufficient,
 * clear the appropriate field(s) and return RETRY.  COWs require that
 * first_shared be 0, while page allocations (or frees) require that
 * shared be 0.  Renames require that both be 0.
 *
 * fs->first_object must be held on call.
 */
static
int
vm_fault_object(struct faultstate *fs, vm_pindex_t first_pindex,
		vm_prot_t fault_type, int allow_nofault)
{
	vm_object_t next_object;
	vm_pindex_t pindex;
	int error;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(fs->first_object));
	fs->prot = fs->first_prot;
	fs->object = fs->first_object;
	pindex = first_pindex;

	vm_object_chain_acquire(fs->first_object, fs->shared);
	vm_object_pip_add(fs->first_object, 1);

	/* 
	 * If a read fault occurs we try to make the page writable if
	 * possible.  There are three cases where we cannot make the
	 * page mapping writable:
	 *
	 * (1) The mapping is read-only or the VM object is read-only,
	 *     fs->prot above will simply not have VM_PROT_WRITE set.
	 *
	 * (2) If the mapping is a virtual page table we need to be able
	 *     to detect writes so we can set VPTE_M in the virtual page
	 *     table.
	 *
	 * (3) If the VM page is read-only or copy-on-write, upgrading would
	 *     just result in an unnecessary COW fault.
	 *
	 * VM_PROT_VPAGED is set if faulting via a virtual page table and
	 * causes adjustments to the 'M'odify bit to also turn off write
	 * access to force a re-fault.
	 */
	if (fs->entry->maptype == VM_MAPTYPE_VPAGETABLE) {
		if ((fault_type & VM_PROT_WRITE) == 0)
			fs->prot &= ~VM_PROT_WRITE;
	}

	if (curthread->td_lwp && curthread->td_lwp->lwp_vmspace &&
	    pmap_emulate_ad_bits(&curthread->td_lwp->lwp_vmspace->vm_pmap)) {
		if ((fault_type & VM_PROT_WRITE) == 0)
			fs->prot &= ~VM_PROT_WRITE;
	}

	/* vm_object_hold(fs->object); implied b/c object == first_object */

	for (;;) {
		/*
		 * The entire backing chain from first_object to object
		 * inclusive is chainlocked.
		 *
		 * If the object is dead, we stop here
		 */
		if (fs->object->flags & OBJ_DEAD) {
			vm_object_pip_wakeup(fs->first_object);
			vm_object_chain_release_all(fs->first_object,
						    fs->object);
			if (fs->object != fs->first_object)
				vm_object_drop(fs->object);
			unlock_and_deallocate(fs);
			return (KERN_PROTECTION_FAILURE);
		}

		/*
		 * See if the page is resident.  Wait/Retry if the page is
		 * busy (lots of stuff may have changed so we can't continue
		 * in that case).
		 *
		 * We can theoretically allow the soft-busy case on a read
		 * fault if the page is marked valid, but since such
		 * pages are typically already pmap'd, putting that
		 * special case in might be more effort then it is
		 * worth.  We cannot under any circumstances mess
		 * around with a vm_page_t->busy page except, perhaps,
		 * to pmap it.
		 */
		fs->m = vm_page_lookup_busy_try(fs->object, pindex,
						TRUE, &error);
		if (error) {
			vm_object_pip_wakeup(fs->first_object);
			vm_object_chain_release_all(fs->first_object,
						    fs->object);
			if (fs->object != fs->first_object)
				vm_object_drop(fs->object);
			unlock_things(fs);
			vm_page_sleep_busy(fs->m, TRUE, "vmpfw");
			mycpu->gd_cnt.v_intrans++;
			/*vm_object_deallocate(fs->first_object);*/
			/*fs->first_object = NULL;*/
			fs->m = NULL;
			return (KERN_TRY_AGAIN);
		}
		if (fs->m) {
			/*
			 * The page is busied for us.
			 *
			 * If reactivating a page from PQ_CACHE we may have
			 * to rate-limit.
			 */
			int queue = fs->m->queue;
			vm_page_unqueue_nowakeup(fs->m);

			if ((queue - fs->m->pc) == PQ_CACHE && 
			    vm_page_count_severe()) {
				vm_page_activate(fs->m);
				vm_page_wakeup(fs->m);
				fs->m = NULL;
				vm_object_pip_wakeup(fs->first_object);
				vm_object_chain_release_all(fs->first_object,
							    fs->object);
				if (fs->object != fs->first_object)
					vm_object_drop(fs->object);
				unlock_and_deallocate(fs);
				if (allow_nofault == 0 ||
				    (curthread->td_flags & TDF_NOFAULT) == 0) {
					vm_wait_pfault();
				}
				return (KERN_TRY_AGAIN);
			}

			/*
			 * If it still isn't completely valid (readable),
			 * or if a read-ahead-mark is set on the VM page,
			 * jump to readrest, else we found the page and
			 * can return.
			 *
			 * We can release the spl once we have marked the
			 * page busy.
			 */
			if (fs->m->object != &kernel_object) {
				if ((fs->m->valid & VM_PAGE_BITS_ALL) !=
				    VM_PAGE_BITS_ALL) {
					goto readrest;
				}
				if (fs->m->flags & PG_RAM) {
					if (debug_cluster)
						kprintf("R");
					vm_page_flag_clear(fs->m, PG_RAM);
					goto readrest;
				}
			}
			break; /* break to PAGE HAS BEEN FOUND */
		}

		/*
		 * Page is not resident, If this is the search termination
		 * or the pager might contain the page, allocate a new page.
		 */
		if (TRYPAGER(fs) || fs->object == fs->first_object) {
			/*
			 * Allocating, must be exclusive.
			 */
			if (fs->object == fs->first_object &&
			    fs->first_shared) {
				fs->first_shared = 0;
				vm_object_pip_wakeup(fs->first_object);
				vm_object_chain_release_all(fs->first_object,
							    fs->object);
				if (fs->object != fs->first_object)
					vm_object_drop(fs->object);
				unlock_and_deallocate(fs);
				return (KERN_TRY_AGAIN);
			}
			if (fs->object != fs->first_object &&
			    fs->shared) {
				fs->first_shared = 0;
				fs->shared = 0;
				vm_object_pip_wakeup(fs->first_object);
				vm_object_chain_release_all(fs->first_object,
							    fs->object);
				if (fs->object != fs->first_object)
					vm_object_drop(fs->object);
				unlock_and_deallocate(fs);
				return (KERN_TRY_AGAIN);
			}

			/*
			 * If the page is beyond the object size we fail
			 */
			if (pindex >= fs->object->size) {
				vm_object_pip_wakeup(fs->first_object);
				vm_object_chain_release_all(fs->first_object,
							    fs->object);
				if (fs->object != fs->first_object)
					vm_object_drop(fs->object);
				unlock_and_deallocate(fs);
				return (KERN_PROTECTION_FAILURE);
			}

			/*
			 * Allocate a new page for this object/offset pair.
			 *
			 * It is possible for the allocation to race, so
			 * handle the case.
			 */
			fs->m = NULL;
			if (!vm_page_count_severe()) {
				fs->m = vm_page_alloc(fs->object, pindex,
				    ((fs->vp || fs->object->backing_object) ?
					VM_ALLOC_NULL_OK | VM_ALLOC_NORMAL :
					VM_ALLOC_NULL_OK | VM_ALLOC_NORMAL |
					VM_ALLOC_USE_GD | VM_ALLOC_ZERO));
			}
			if (fs->m == NULL) {
				vm_object_pip_wakeup(fs->first_object);
				vm_object_chain_release_all(fs->first_object,
							    fs->object);
				if (fs->object != fs->first_object)
					vm_object_drop(fs->object);
				unlock_and_deallocate(fs);
				if (allow_nofault == 0 ||
				    (curthread->td_flags & TDF_NOFAULT) == 0) {
					vm_wait_pfault();
				}
				return (KERN_TRY_AGAIN);
			}

			/*
			 * Fall through to readrest.  We have a new page which
			 * will have to be paged (since m->valid will be 0).
			 */
		}

readrest:
		/*
		 * We have found an invalid or partially valid page, a
		 * page with a read-ahead mark which might be partially or
		 * fully valid (and maybe dirty too), or we have allocated
		 * a new page.
		 *
		 * Attempt to fault-in the page if there is a chance that the
		 * pager has it, and potentially fault in additional pages
		 * at the same time.
		 *
		 * If TRYPAGER is true then fs.m will be non-NULL and busied
		 * for us.
		 */
		if (TRYPAGER(fs)) {
			int rv;
			int seqaccess;
			u_char behavior = vm_map_entry_behavior(fs->entry);

			if (behavior == MAP_ENTRY_BEHAV_RANDOM)
				seqaccess = 0;
			else
				seqaccess = -1;

			/*
			 * Doing I/O may synchronously insert additional
			 * pages so we can't be shared at this point either.
			 *
			 * NOTE: We can't free fs->m here in the allocated
			 *	 case (fs->object != fs->first_object) as
			 *	 this would require an exclusively locked
			 *	 VM object.
			 */
			if (fs->object == fs->first_object &&
			    fs->first_shared) {
				vm_page_deactivate(fs->m);
				vm_page_wakeup(fs->m);
				fs->m = NULL;
				fs->first_shared = 0;
				vm_object_pip_wakeup(fs->first_object);
				vm_object_chain_release_all(fs->first_object,
							    fs->object);
				if (fs->object != fs->first_object)
					vm_object_drop(fs->object);
				unlock_and_deallocate(fs);
				return (KERN_TRY_AGAIN);
			}
			if (fs->object != fs->first_object &&
			    fs->shared) {
				vm_page_deactivate(fs->m);
				vm_page_wakeup(fs->m);
				fs->m = NULL;
				fs->first_shared = 0;
				fs->shared = 0;
				vm_object_pip_wakeup(fs->first_object);
				vm_object_chain_release_all(fs->first_object,
							    fs->object);
				if (fs->object != fs->first_object)
					vm_object_drop(fs->object);
				unlock_and_deallocate(fs);
				return (KERN_TRY_AGAIN);
			}

			/*
			 * Avoid deadlocking against the map when doing I/O.
			 * fs.object and the page is PG_BUSY'd.
			 *
			 * NOTE: Once unlocked, fs->entry can become stale
			 *	 so this will NULL it out.
			 *
			 * NOTE: fs->entry is invalid until we relock the
			 *	 map and verify that the timestamp has not
			 *	 changed.
			 */
			unlock_map(fs);

			/*
			 * Acquire the page data.  We still hold a ref on
			 * fs.object and the page has been PG_BUSY's.
			 *
			 * The pager may replace the page (for example, in
			 * order to enter a fictitious page into the
			 * object).  If it does so it is responsible for
			 * cleaning up the passed page and properly setting
			 * the new page PG_BUSY.
			 *
			 * If we got here through a PG_RAM read-ahead
			 * mark the page may be partially dirty and thus
			 * not freeable.  Don't bother checking to see
			 * if the pager has the page because we can't free
			 * it anyway.  We have to depend on the get_page
			 * operation filling in any gaps whether there is
			 * backing store or not.
			 */
			rv = vm_pager_get_page(fs->object, &fs->m, seqaccess);

			if (rv == VM_PAGER_OK) {
				/*
				 * Relookup in case pager changed page. Pager
				 * is responsible for disposition of old page
				 * if moved.
				 *
				 * XXX other code segments do relookups too.
				 * It's a bad abstraction that needs to be
				 * fixed/removed.
				 */
				fs->m = vm_page_lookup(fs->object, pindex);
				if (fs->m == NULL) {
					vm_object_pip_wakeup(fs->first_object);
					vm_object_chain_release_all(
						fs->first_object, fs->object);
					if (fs->object != fs->first_object)
						vm_object_drop(fs->object);
					unlock_and_deallocate(fs);
					return (KERN_TRY_AGAIN);
				}
				++fs->hardfault;
				break; /* break to PAGE HAS BEEN FOUND */
			}

			/*
			 * Remove the bogus page (which does not exist at this
			 * object/offset); before doing so, we must get back
			 * our object lock to preserve our invariant.
			 *
			 * Also wake up any other process that may want to bring
			 * in this page.
			 *
			 * If this is the top-level object, we must leave the
			 * busy page to prevent another process from rushing
			 * past us, and inserting the page in that object at
			 * the same time that we are.
			 */
			if (rv == VM_PAGER_ERROR) {
				if (curproc) {
					kprintf("vm_fault: pager read error, "
						"pid %d (%s)\n",
						curproc->p_pid,
						curproc->p_comm);
				} else {
					kprintf("vm_fault: pager read error, "
						"thread %p (%s)\n",
						curthread,
						curproc->p_comm);
				}
			}

			/*
			 * Data outside the range of the pager or an I/O error
			 *
			 * The page may have been wired during the pagein,
			 * e.g. by the buffer cache, and cannot simply be
			 * freed.  Call vnode_pager_freepage() to deal with it.
			 *
			 * Also note that we cannot free the page if we are
			 * holding the related object shared. XXX not sure
			 * what to do in that case.
			 */
			if (fs->object != fs->first_object) {
				vnode_pager_freepage(fs->m);
				fs->m = NULL;
				/*
				 * XXX - we cannot just fall out at this
				 * point, m has been freed and is invalid!
				 */
			}
			/*
			 * XXX - the check for kernel_map is a kludge to work
			 * around having the machine panic on a kernel space
			 * fault w/ I/O error.
			 */
			if (((fs->map != &kernel_map) &&
			    (rv == VM_PAGER_ERROR)) || (rv == VM_PAGER_BAD)) {
				if (fs->m) {
					if (fs->first_shared) {
						vm_page_deactivate(fs->m);
						vm_page_wakeup(fs->m);
					} else {
						vnode_pager_freepage(fs->m);
					}
					fs->m = NULL;
				}
				vm_object_pip_wakeup(fs->first_object);
				vm_object_chain_release_all(fs->first_object,
							    fs->object);
				if (fs->object != fs->first_object)
					vm_object_drop(fs->object);
				unlock_and_deallocate(fs);
				if (rv == VM_PAGER_ERROR)
					return (KERN_FAILURE);
				else
					return (KERN_PROTECTION_FAILURE);
				/* NOT REACHED */
			}
		}

		/*
		 * We get here if the object has a default pager (or unwiring) 
		 * or the pager doesn't have the page.
		 *
		 * fs->first_m will be used for the COW unless we find a
		 * deeper page to be mapped read-only, in which case the
		 * unlock*(fs) will free first_m.
		 */
		if (fs->object == fs->first_object)
			fs->first_m = fs->m;

		/*
		 * Move on to the next object.  The chain lock should prevent
		 * the backing_object from getting ripped out from under us.
		 *
		 * The object lock for the next object is governed by
		 * fs->shared.
		 */
		if ((next_object = fs->object->backing_object) != NULL) {
			if (fs->shared)
				vm_object_hold_shared(next_object);
			else
				vm_object_hold(next_object);
			vm_object_chain_acquire(next_object, fs->shared);
			KKASSERT(next_object == fs->object->backing_object);
			pindex += OFF_TO_IDX(fs->object->backing_object_offset);
		}

		if (next_object == NULL) {
			/*
			 * If there's no object left, fill the page in the top
			 * object with zeros.
			 */
			if (fs->object != fs->first_object) {
#if 0
				if (fs->first_object->backing_object !=
				    fs->object) {
					vm_object_hold(fs->first_object->backing_object);
				}
#endif
				vm_object_chain_release_all(
					fs->first_object->backing_object,
					fs->object);
#if 0
				if (fs->first_object->backing_object !=
				    fs->object) {
					vm_object_drop(fs->first_object->backing_object);
				}
#endif
				vm_object_pip_wakeup(fs->object);
				vm_object_drop(fs->object);
				fs->object = fs->first_object;
				pindex = first_pindex;
				fs->m = fs->first_m;
			}
			fs->first_m = NULL;

			/*
			 * Zero the page if necessary and mark it valid.
			 */
			if ((fs->m->flags & PG_ZERO) == 0) {
				vm_page_zero_fill(fs->m);
			} else {
#ifdef PMAP_DEBUG
				pmap_page_assertzero(VM_PAGE_TO_PHYS(fs->m));
#endif
				vm_page_flag_clear(fs->m, PG_ZERO);
				mycpu->gd_cnt.v_ozfod++;
			}
			mycpu->gd_cnt.v_zfod++;
			fs->m->valid = VM_PAGE_BITS_ALL;
			break;	/* break to PAGE HAS BEEN FOUND */
		}
		if (fs->object != fs->first_object) {
			vm_object_pip_wakeup(fs->object);
			vm_object_lock_swap();
			vm_object_drop(fs->object);
		}
		KASSERT(fs->object != next_object,
			("object loop %p", next_object));
		fs->object = next_object;
		vm_object_pip_add(fs->object, 1);
	}

	/*
	 * PAGE HAS BEEN FOUND. [Loop invariant still holds -- the object lock
	 * is held.]
	 *
	 * object still held.
	 *
	 * local shared variable may be different from fs->shared.
	 *
	 * If the page is being written, but isn't already owned by the
	 * top-level object, we have to copy it into a new page owned by the
	 * top-level object.
	 */
	KASSERT((fs->m->flags & PG_BUSY) != 0,
		("vm_fault: not busy after main loop"));

	if (fs->object != fs->first_object) {
		/*
		 * We only really need to copy if we want to write it.
		 */
		if (fault_type & VM_PROT_WRITE) {
			/*
			 * This allows pages to be virtually copied from a 
			 * backing_object into the first_object, where the 
			 * backing object has no other refs to it, and cannot
			 * gain any more refs.  Instead of a bcopy, we just 
			 * move the page from the backing object to the 
			 * first object.  Note that we must mark the page 
			 * dirty in the first object so that it will go out 
			 * to swap when needed.
			 */
			if (
				/*
				 * Must be holding exclusive locks
				 */
				fs->first_shared == 0 &&
				fs->shared == 0 &&
				/*
				 * Map, if present, has not changed
				 */
				(fs->map == NULL ||
				fs->map_generation == fs->map->timestamp) &&
				/*
				 * Only one shadow object
				 */
				(fs->object->shadow_count == 1) &&
				/*
				 * No COW refs, except us
				 */
				(fs->object->ref_count == 1) &&
				/*
				 * No one else can look this object up
				 */
				(fs->object->handle == NULL) &&
				/*
				 * No other ways to look the object up
				 */
				((fs->object->type == OBJT_DEFAULT) ||
				 (fs->object->type == OBJT_SWAP)) &&
				/*
				 * We don't chase down the shadow chain
				 */
				(fs->object == fs->first_object->backing_object) &&

				/*
				 * grab the lock if we need to
				 */
				(fs->lookup_still_valid ||
				 fs->map == NULL ||
				 lockmgr(&fs->map->lock, LK_EXCLUSIVE|LK_NOWAIT) == 0)
			    ) {
				/*
				 * (first_m) and (m) are both busied.  We have
				 * move (m) into (first_m)'s object/pindex
				 * in an atomic fashion, then free (first_m).
				 *
				 * first_object is held so second remove
				 * followed by the rename should wind
				 * up being atomic.  vm_page_free() might
				 * block so we don't do it until after the
				 * rename.
				 */
				fs->lookup_still_valid = 1;
				vm_page_protect(fs->first_m, VM_PROT_NONE);
				vm_page_remove(fs->first_m);
				vm_page_rename(fs->m, fs->first_object,
					       first_pindex);
				vm_page_free(fs->first_m);
				fs->first_m = fs->m;
				fs->m = NULL;
				mycpu->gd_cnt.v_cow_optim++;
			} else {
				/*
				 * Oh, well, lets copy it.
				 *
				 * Why are we unmapping the original page
				 * here?  Well, in short, not all accessors
				 * of user memory go through the pmap.  The
				 * procfs code doesn't have access user memory
				 * via a local pmap, so vm_fault_page*()
				 * can't call pmap_enter().  And the umtx*()
				 * code may modify the COW'd page via a DMAP
				 * or kernel mapping and not via the pmap,
				 * leaving the original page still mapped
				 * read-only into the pmap.
				 *
				 * So we have to remove the page from at
				 * least the current pmap if it is in it.
				 * Just remove it from all pmaps.
				 */
				KKASSERT(fs->first_shared == 0);
				vm_page_copy(fs->m, fs->first_m);
				vm_page_protect(fs->m, VM_PROT_NONE);
				vm_page_event(fs->m, VMEVENT_COW);
			}

			/*
			 * We no longer need the old page or object.
			 */
			if (fs->m)
				release_page(fs);

			/*
			 * We intend to revert to first_object, undo the
			 * chain lock through to that.
			 */
#if 0
			if (fs->first_object->backing_object != fs->object)
				vm_object_hold(fs->first_object->backing_object);
#endif
			vm_object_chain_release_all(
					fs->first_object->backing_object,
					fs->object);
#if 0
			if (fs->first_object->backing_object != fs->object)
				vm_object_drop(fs->first_object->backing_object);
#endif

			/*
			 * fs->object != fs->first_object due to above 
			 * conditional
			 */
			vm_object_pip_wakeup(fs->object);
			vm_object_drop(fs->object);

			/*
			 * Only use the new page below...
			 */
			mycpu->gd_cnt.v_cow_faults++;
			fs->m = fs->first_m;
			fs->object = fs->first_object;
			pindex = first_pindex;
		} else {
			/*
			 * If it wasn't a write fault avoid having to copy
			 * the page by mapping it read-only.
			 */
			fs->prot &= ~VM_PROT_WRITE;
		}
	}

	/*
	 * Relock the map if necessary, then check the generation count.
	 * relock_map() will update fs->timestamp to account for the
	 * relocking if necessary.
	 *
	 * If the count has changed after relocking then all sorts of
	 * crap may have happened and we have to retry.
	 *
	 * NOTE: The relock_map() can fail due to a deadlock against
	 *	 the vm_page we are holding BUSY.
	 */
	if (fs->lookup_still_valid == FALSE && fs->map) {
		if (relock_map(fs) ||
		    fs->map->timestamp != fs->map_generation) {
			release_page(fs);
			vm_object_pip_wakeup(fs->first_object);
			vm_object_chain_release_all(fs->first_object,
						    fs->object);
			if (fs->object != fs->first_object)
				vm_object_drop(fs->object);
			unlock_and_deallocate(fs);
			return (KERN_TRY_AGAIN);
		}
	}

	/*
	 * If the fault is a write, we know that this page is being
	 * written NOW so dirty it explicitly to save on pmap_is_modified()
	 * calls later.
	 *
	 * If this is a NOSYNC mmap we do not want to set PG_NOSYNC
	 * if the page is already dirty to prevent data written with
	 * the expectation of being synced from not being synced.
	 * Likewise if this entry does not request NOSYNC then make
	 * sure the page isn't marked NOSYNC.  Applications sharing
	 * data should use the same flags to avoid ping ponging.
	 *
	 * Also tell the backing pager, if any, that it should remove
	 * any swap backing since the page is now dirty.
	 */
	vm_page_activate(fs->m);
	if (fs->prot & VM_PROT_WRITE) {
		vm_object_set_writeable_dirty(fs->m->object);
		vm_set_nosync(fs->m, fs->entry);
		if (fs->fault_flags & VM_FAULT_DIRTY) {
			vm_page_dirty(fs->m);
			swap_pager_unswapped(fs->m);
		}
	}

	vm_object_pip_wakeup(fs->first_object);
	vm_object_chain_release_all(fs->first_object, fs->object);
	if (fs->object != fs->first_object)
		vm_object_drop(fs->object);

	/*
	 * Page had better still be busy.  We are still locked up and 
	 * fs->object will have another PIP reference if it is not equal
	 * to fs->first_object.
	 */
	KASSERT(fs->m->flags & PG_BUSY,
		("vm_fault: page %p not busy!", fs->m));

	/*
	 * Sanity check: page must be completely valid or it is not fit to
	 * map into user space.  vm_pager_get_pages() ensures this.
	 */
	if (fs->m->valid != VM_PAGE_BITS_ALL) {
		vm_page_zero_invalid(fs->m, TRUE);
		kprintf("Warning: page %p partially invalid on fault\n", fs->m);
	}
	vm_page_flag_clear(fs->m, PG_ZERO);

	return (KERN_SUCCESS);
}

/*
 * Hold each of the physical pages that are mapped by the specified range of
 * virtual addresses, ["addr", "addr" + "len"), if those mappings are valid
 * and allow the specified types of access, "prot".  If all of the implied
 * pages are successfully held, then the number of held pages is returned
 * together with pointers to those pages in the array "ma".  However, if any
 * of the pages cannot be held, -1 is returned.
 */
int
vm_fault_quick_hold_pages(vm_map_t map, vm_offset_t addr, vm_size_t len,
    vm_prot_t prot, vm_page_t *ma, int max_count)
{
	vm_offset_t start, end;
	int i, npages, error;

	start = trunc_page(addr);
	end = round_page(addr + len);

	npages = howmany(end - start, PAGE_SIZE);

	if (npages > max_count)
		return -1;

	for (i = 0; i < npages; i++) {
		// XXX error handling
		ma[i] = vm_fault_page_quick(start + (i * PAGE_SIZE),
			prot,
			&error);
	}

	return npages;
}

/*
 * Wire down a range of virtual addresses in a map.  The entry in question
 * should be marked in-transition and the map must be locked.  We must
 * release the map temporarily while faulting-in the page to avoid a
 * deadlock.  Note that the entry may be clipped while we are blocked but
 * will never be freed.
 *
 * No requirements.
 */
int
vm_fault_wire(vm_map_t map, vm_map_entry_t entry, boolean_t user_wire)
{
	boolean_t fictitious;
	vm_offset_t start;
	vm_offset_t end;
	vm_offset_t va;
	vm_paddr_t pa;
	vm_page_t m;
	pmap_t pmap;
	int rv;

	lwkt_gettoken(&map->token);

	pmap = vm_map_pmap(map);
	start = entry->start;
	end = entry->end;
	fictitious = entry->object.vm_object &&
			((entry->object.vm_object->type == OBJT_DEVICE) ||
			 (entry->object.vm_object->type == OBJT_MGTDEVICE));
	if (entry->eflags & MAP_ENTRY_KSTACK)
		start += PAGE_SIZE;
	map->timestamp++;
	vm_map_unlock(map);

	/*
	 * We simulate a fault to get the page and enter it in the physical
	 * map.
	 */
	for (va = start; va < end; va += PAGE_SIZE) {
		if (user_wire) {
			rv = vm_fault(map, va, VM_PROT_READ, 
					VM_FAULT_USER_WIRE);
		} else {
			rv = vm_fault(map, va, VM_PROT_READ|VM_PROT_WRITE,
					VM_FAULT_CHANGE_WIRING);
		}
		if (rv) {
			while (va > start) {
				va -= PAGE_SIZE;
				if ((pa = pmap_extract(pmap, va)) == 0)
					continue;
				pmap_change_wiring(pmap, va, FALSE, entry);
				if (!fictitious) {
					m = PHYS_TO_VM_PAGE(pa);
					vm_page_busy_wait(m, FALSE, "vmwrpg");
					vm_page_unwire(m, 1);
					vm_page_wakeup(m);
				}
			}
			goto done;
		}
	}
	rv = KERN_SUCCESS;
done:
	vm_map_lock(map);
	lwkt_reltoken(&map->token);
	return (rv);
}

/*
 * Unwire a range of virtual addresses in a map.  The map should be
 * locked.
 */
void
vm_fault_unwire(vm_map_t map, vm_map_entry_t entry)
{
	boolean_t fictitious;
	vm_offset_t start;
	vm_offset_t end;
	vm_offset_t va;
	vm_paddr_t pa;
	vm_page_t m;
	pmap_t pmap;

	lwkt_gettoken(&map->token);

	pmap = vm_map_pmap(map);
	start = entry->start;
	end = entry->end;
	fictitious = entry->object.vm_object &&
			((entry->object.vm_object->type == OBJT_DEVICE) ||
			 (entry->object.vm_object->type == OBJT_MGTDEVICE));
	if (entry->eflags & MAP_ENTRY_KSTACK)
		start += PAGE_SIZE;

	/*
	 * Since the pages are wired down, we must be able to get their
	 * mappings from the physical map system.
	 */
	for (va = start; va < end; va += PAGE_SIZE) {
		pa = pmap_extract(pmap, va);
		if (pa != 0) {
			pmap_change_wiring(pmap, va, FALSE, entry);
			if (!fictitious) {
				m = PHYS_TO_VM_PAGE(pa);
				vm_page_busy_wait(m, FALSE, "vmwupg");
				vm_page_unwire(m, 1);
				vm_page_wakeup(m);
			}
		}
	}
	lwkt_reltoken(&map->token);
}

/*
 * Copy all of the pages from a wired-down map entry to another.
 *
 * The source and destination maps must be locked for write.
 * The source and destination maps token must be held
 * The source map entry must be wired down (or be a sharing map
 * entry corresponding to a main map entry that is wired down).
 *
 * No other requirements.
 *
 * XXX do segment optimization
 */
void
vm_fault_copy_entry(vm_map_t dst_map, vm_map_t src_map,
		    vm_map_entry_t dst_entry, vm_map_entry_t src_entry)
{
	vm_object_t dst_object;
	vm_object_t src_object;
	vm_ooffset_t dst_offset;
	vm_ooffset_t src_offset;
	vm_prot_t prot;
	vm_offset_t vaddr;
	vm_page_t dst_m;
	vm_page_t src_m;

	src_object = src_entry->object.vm_object;
	src_offset = src_entry->offset;

	/*
	 * Create the top-level object for the destination entry. (Doesn't
	 * actually shadow anything - we copy the pages directly.)
	 */
	vm_map_entry_allocate_object(dst_entry);
	dst_object = dst_entry->object.vm_object;

	prot = dst_entry->max_protection;

	/*
	 * Loop through all of the pages in the entry's range, copying each
	 * one from the source object (it should be there) to the destination
	 * object.
	 */
	vm_object_hold(src_object);
	vm_object_hold(dst_object);
	for (vaddr = dst_entry->start, dst_offset = 0;
	    vaddr < dst_entry->end;
	    vaddr += PAGE_SIZE, dst_offset += PAGE_SIZE) {

		/*
		 * Allocate a page in the destination object
		 */
		do {
			dst_m = vm_page_alloc(dst_object,
					      OFF_TO_IDX(dst_offset),
					      VM_ALLOC_NORMAL);
			if (dst_m == NULL) {
				vm_wait(0);
			}
		} while (dst_m == NULL);

		/*
		 * Find the page in the source object, and copy it in.
		 * (Because the source is wired down, the page will be in
		 * memory.)
		 */
		src_m = vm_page_lookup(src_object,
				       OFF_TO_IDX(dst_offset + src_offset));
		if (src_m == NULL)
			panic("vm_fault_copy_wired: page missing");

		vm_page_copy(src_m, dst_m);
		vm_page_event(src_m, VMEVENT_COW);

		/*
		 * Enter it in the pmap...
		 */

		vm_page_flag_clear(dst_m, PG_ZERO);
		pmap_enter(dst_map->pmap, vaddr, dst_m, prot, FALSE, dst_entry);

		/*
		 * Mark it no longer busy, and put it on the active list.
		 */
		vm_page_activate(dst_m);
		vm_page_wakeup(dst_m);
	}
	vm_object_drop(dst_object);
	vm_object_drop(src_object);
}

#if 0

/*
 * This routine checks around the requested page for other pages that
 * might be able to be faulted in.  This routine brackets the viable
 * pages for the pages to be paged in.
 *
 * Inputs:
 *	m, rbehind, rahead
 *
 * Outputs:
 *  marray (array of vm_page_t), reqpage (index of requested page)
 *
 * Return value:
 *  number of pages in marray
 */
static int
vm_fault_additional_pages(vm_page_t m, int rbehind, int rahead,
			  vm_page_t *marray, int *reqpage)
{
	int i,j;
	vm_object_t object;
	vm_pindex_t pindex, startpindex, endpindex, tpindex;
	vm_page_t rtm;
	int cbehind, cahead;

	object = m->object;
	pindex = m->pindex;

	/*
	 * we don't fault-ahead for device pager
	 */
	if ((object->type == OBJT_DEVICE) ||
	    (object->type == OBJT_MGTDEVICE)) {
		*reqpage = 0;
		marray[0] = m;
		return 1;
	}

	/*
	 * if the requested page is not available, then give up now
	 */
	if (!vm_pager_has_page(object, pindex, &cbehind, &cahead)) {
		*reqpage = 0;	/* not used by caller, fix compiler warn */
		return 0;
	}

	if ((cbehind == 0) && (cahead == 0)) {
		*reqpage = 0;
		marray[0] = m;
		return 1;
	}

	if (rahead > cahead) {
		rahead = cahead;
	}

	if (rbehind > cbehind) {
		rbehind = cbehind;
	}

	/*
	 * Do not do any readahead if we have insufficient free memory.
	 *
	 * XXX code was broken disabled before and has instability
	 * with this conditonal fixed, so shortcut for now.
	 */
	if (burst_fault == 0 || vm_page_count_severe()) {
		marray[0] = m;
		*reqpage = 0;
		return 1;
	}

	/*
	 * scan backward for the read behind pages -- in memory 
	 *
	 * Assume that if the page is not found an interrupt will not
	 * create it.  Theoretically interrupts can only remove (busy)
	 * pages, not create new associations.
	 */
	if (pindex > 0) {
		if (rbehind > pindex) {
			rbehind = pindex;
			startpindex = 0;
		} else {
			startpindex = pindex - rbehind;
		}

		vm_object_hold(object);
		for (tpindex = pindex; tpindex > startpindex; --tpindex) {
			if (vm_page_lookup(object, tpindex - 1))
				break;
		}

		i = 0;
		while (tpindex < pindex) {
			rtm = vm_page_alloc(object, tpindex, VM_ALLOC_SYSTEM |
							     VM_ALLOC_NULL_OK);
			if (rtm == NULL) {
				for (j = 0; j < i; j++) {
					vm_page_free(marray[j]);
				}
				vm_object_drop(object);
				marray[0] = m;
				*reqpage = 0;
				return 1;
			}
			marray[i] = rtm;
			++i;
			++tpindex;
		}
		vm_object_drop(object);
	} else {
		i = 0;
	}

	/*
	 * Assign requested page
	 */
	marray[i] = m;
	*reqpage = i;
	++i;

	/*
	 * Scan forwards for read-ahead pages
	 */
	tpindex = pindex + 1;
	endpindex = tpindex + rahead;
	if (endpindex > object->size)
		endpindex = object->size;

	vm_object_hold(object);
	while (tpindex < endpindex) {
		if (vm_page_lookup(object, tpindex))
			break;
		rtm = vm_page_alloc(object, tpindex, VM_ALLOC_SYSTEM |
						     VM_ALLOC_NULL_OK);
		if (rtm == NULL)
			break;
		marray[i] = rtm;
		++i;
		++tpindex;
	}
	vm_object_drop(object);

	return (i);
}

#endif

/*
 * vm_prefault() provides a quick way of clustering pagefaults into a
 * processes address space.  It is a "cousin" of pmap_object_init_pt,
 * except it runs at page fault time instead of mmap time.
 *
 * vm.fast_fault	Enables pre-faulting zero-fill pages
 *
 * vm.prefault_pages	Number of pages (1/2 negative, 1/2 positive) to
 *			prefault.  Scan stops in either direction when
 *			a page is found to already exist.
 *
 * This code used to be per-platform pmap_prefault().  It is now
 * machine-independent and enhanced to also pre-fault zero-fill pages
 * (see vm.fast_fault) as well as make them writable, which greatly
 * reduces the number of page faults programs incur.
 *
 * Application performance when pre-faulting zero-fill pages is heavily
 * dependent on the application.  Very tiny applications like /bin/echo
 * lose a little performance while applications of any appreciable size
 * gain performance.  Prefaulting multiple pages also reduces SMP
 * congestion and can improve SMP performance significantly.
 *
 * NOTE!  prot may allow writing but this only applies to the top level
 *	  object.  If we wind up mapping a page extracted from a backing
 *	  object we have to make sure it is read-only.
 *
 * NOTE!  The caller has already handled any COW operations on the
 *	  vm_map_entry via the normal fault code.  Do NOT call this
 *	  shortcut unless the normal fault code has run on this entry.
 *
 * The related map must be locked.
 * No other requirements.
 */
static int vm_prefault_pages = 8;
SYSCTL_INT(_vm, OID_AUTO, prefault_pages, CTLFLAG_RW, &vm_prefault_pages, 0,
	   "Maximum number of pages to pre-fault");
static int vm_fast_fault = 1;
SYSCTL_INT(_vm, OID_AUTO, fast_fault, CTLFLAG_RW, &vm_fast_fault, 0,
	   "Burst fault zero-fill regions");

/*
 * Set PG_NOSYNC if the map entry indicates so, but only if the page
 * is not already dirty by other means.  This will prevent passive
 * filesystem syncing as well as 'sync' from writing out the page.
 */
static void
vm_set_nosync(vm_page_t m, vm_map_entry_t entry)
{
	if (entry->eflags & MAP_ENTRY_NOSYNC) {
		if (m->dirty == 0)
			vm_page_flag_set(m, PG_NOSYNC);
	} else {
		vm_page_flag_clear(m, PG_NOSYNC);
	}
}

static void
vm_prefault(pmap_t pmap, vm_offset_t addra, vm_map_entry_t entry, int prot,
	    int fault_flags)
{
	struct lwp *lp;
	vm_page_t m;
	vm_offset_t addr;
	vm_pindex_t index;
	vm_pindex_t pindex;
	vm_object_t object;
	int pprot;
	int i;
	int noneg;
	int nopos;
	int maxpages;

	/*
	 * Get stable max count value, disabled if set to 0
	 */
	maxpages = vm_prefault_pages;
	cpu_ccfence();
	if (maxpages <= 0)
		return;

	/*
	 * We do not currently prefault mappings that use virtual page
	 * tables.  We do not prefault foreign pmaps.
	 */
	if (entry->maptype == VM_MAPTYPE_VPAGETABLE)
		return;
	lp = curthread->td_lwp;
	if (lp == NULL || (pmap != vmspace_pmap(lp->lwp_vmspace)))
		return;

	/*
	 * Limit pre-fault count to 1024 pages.
	 */
	if (maxpages > 1024)
		maxpages = 1024;

	object = entry->object.vm_object;
	KKASSERT(object != NULL);
	KKASSERT(object == entry->object.vm_object);
	vm_object_hold(object);
	vm_object_chain_acquire(object, 0);

	noneg = 0;
	nopos = 0;
	for (i = 0; i < maxpages; ++i) {
		vm_object_t lobject;
		vm_object_t nobject;
		int allocated = 0;
		int error;

		/*
		 * This can eat a lot of time on a heavily contended
		 * machine so yield on the tick if needed.
		 */
		if ((i & 7) == 7)
			lwkt_yield();

		/*
		 * Calculate the page to pre-fault, stopping the scan in
		 * each direction separately if the limit is reached.
		 */
		if (i & 1) {
			if (noneg)
				continue;
			addr = addra - ((i + 1) >> 1) * PAGE_SIZE;
		} else {
			if (nopos)
				continue;
			addr = addra + ((i + 2) >> 1) * PAGE_SIZE;
		}
		if (addr < entry->start) {
			noneg = 1;
			if (noneg && nopos)
				break;
			continue;
		}
		if (addr >= entry->end) {
			nopos = 1;
			if (noneg && nopos)
				break;
			continue;
		}

		/*
		 * Skip pages already mapped, and stop scanning in that
		 * direction.  When the scan terminates in both directions
		 * we are done.
		 */
		if (pmap_prefault_ok(pmap, addr) == 0) {
			if (i & 1)
				noneg = 1;
			else
				nopos = 1;
			if (noneg && nopos)
				break;
			continue;
		}

		/*
		 * Follow the VM object chain to obtain the page to be mapped
		 * into the pmap.
		 *
		 * If we reach the terminal object without finding a page
		 * and we determine it would be advantageous, then allocate
		 * a zero-fill page for the base object.  The base object
		 * is guaranteed to be OBJT_DEFAULT for this case.
		 *
		 * In order to not have to check the pager via *haspage*()
		 * we stop if any non-default object is encountered.  e.g.
		 * a vnode or swap object would stop the loop.
		 */
		index = ((addr - entry->start) + entry->offset) >> PAGE_SHIFT;
		lobject = object;
		pindex = index;
		pprot = prot;

		KKASSERT(lobject == entry->object.vm_object);
		/*vm_object_hold(lobject); implied */

		while ((m = vm_page_lookup_busy_try(lobject, pindex,
						    TRUE, &error)) == NULL) {
			if (lobject->type != OBJT_DEFAULT)
				break;
			if (lobject->backing_object == NULL) {
				if (vm_fast_fault == 0)
					break;
				if ((prot & VM_PROT_WRITE) == 0 ||
				    vm_page_count_min(0)) {
					break;
				}

				/*
				 * NOTE: Allocated from base object
				 */
				m = vm_page_alloc(object, index,
						  VM_ALLOC_NORMAL |
						  VM_ALLOC_ZERO |
						  VM_ALLOC_USE_GD |
						  VM_ALLOC_NULL_OK);
				if (m == NULL)
					break;
				allocated = 1;
				pprot = prot;
				/* lobject = object .. not needed */
				break;
			}
			if (lobject->backing_object_offset & PAGE_MASK)
				break;
			nobject = lobject->backing_object;
			vm_object_hold(nobject);
			KKASSERT(nobject == lobject->backing_object);
			pindex += lobject->backing_object_offset >> PAGE_SHIFT;
			if (lobject != object) {
				vm_object_lock_swap();
				vm_object_drop(lobject);
			}
			lobject = nobject;
			pprot &= ~VM_PROT_WRITE;
			vm_object_chain_acquire(lobject, 0);
		}

		/*
		 * NOTE: A non-NULL (m) will be associated with lobject if
		 *	 it was found there, otherwise it is probably a
		 *	 zero-fill page associated with the base object.
		 *
		 * Give-up if no page is available.
		 */
		if (m == NULL) {
			if (lobject != object) {
#if 0
				if (object->backing_object != lobject)
					vm_object_hold(object->backing_object);
#endif
				vm_object_chain_release_all(
					object->backing_object, lobject);
#if 0
				if (object->backing_object != lobject)
					vm_object_drop(object->backing_object);
#endif
				vm_object_drop(lobject);
			}
			break;
		}

		/*
		 * The object must be marked dirty if we are mapping a
		 * writable page.  m->object is either lobject or object,
		 * both of which are still held.  Do this before we
		 * potentially drop the object.
		 */
		if (pprot & VM_PROT_WRITE)
			vm_object_set_writeable_dirty(m->object);

		/*
		 * Do not conditionalize on PG_RAM.  If pages are present in
		 * the VM system we assume optimal caching.  If caching is
		 * not optimal the I/O gravy train will be restarted when we
		 * hit an unavailable page.  We do not want to try to restart
		 * the gravy train now because we really don't know how much
		 * of the object has been cached.  The cost for restarting
		 * the gravy train should be low (since accesses will likely
		 * be I/O bound anyway).
		 */
		if (lobject != object) {
#if 0
			if (object->backing_object != lobject)
				vm_object_hold(object->backing_object);
#endif
			vm_object_chain_release_all(object->backing_object,
						    lobject);
#if 0
			if (object->backing_object != lobject)
				vm_object_drop(object->backing_object);
#endif
			vm_object_drop(lobject);
		}

		/*
		 * Enter the page into the pmap if appropriate.  If we had
		 * allocated the page we have to place it on a queue.  If not
		 * we just have to make sure it isn't on the cache queue
		 * (pages on the cache queue are not allowed to be mapped).
		 */
		if (allocated) {
			/*
			 * Page must be zerod.
			 */
			if ((m->flags & PG_ZERO) == 0) {
				vm_page_zero_fill(m);
			} else {
#ifdef PMAP_DEBUG
				pmap_page_assertzero(
						VM_PAGE_TO_PHYS(m));
#endif
				vm_page_flag_clear(m, PG_ZERO);
				mycpu->gd_cnt.v_ozfod++;
			}
			mycpu->gd_cnt.v_zfod++;
			m->valid = VM_PAGE_BITS_ALL;

			/*
			 * Handle dirty page case
			 */
			if (pprot & VM_PROT_WRITE)
				vm_set_nosync(m, entry);
			pmap_enter(pmap, addr, m, pprot, 0, entry);
			mycpu->gd_cnt.v_vm_faults++;
			if (curthread->td_lwp)
				++curthread->td_lwp->lwp_ru.ru_minflt;
			vm_page_deactivate(m);
			if (pprot & VM_PROT_WRITE) {
				/*vm_object_set_writeable_dirty(m->object);*/
				vm_set_nosync(m, entry);
				if (fault_flags & VM_FAULT_DIRTY) {
					vm_page_dirty(m);
					/*XXX*/
					swap_pager_unswapped(m);
				}
			}
			vm_page_wakeup(m);
		} else if (error) {
			/* couldn't busy page, no wakeup */
		} else if (
		    ((m->valid & VM_PAGE_BITS_ALL) == VM_PAGE_BITS_ALL) &&
		    (m->flags & PG_FICTITIOUS) == 0) {
			/*
			 * A fully valid page not undergoing soft I/O can
			 * be immediately entered into the pmap.
			 */
			if ((m->queue - m->pc) == PQ_CACHE)
				vm_page_deactivate(m);
			if (pprot & VM_PROT_WRITE) {
				/*vm_object_set_writeable_dirty(m->object);*/
				vm_set_nosync(m, entry);
				if (fault_flags & VM_FAULT_DIRTY) {
					vm_page_dirty(m);
					/*XXX*/
					swap_pager_unswapped(m);
				}
			}
			if (pprot & VM_PROT_WRITE)
				vm_set_nosync(m, entry);
			pmap_enter(pmap, addr, m, pprot, 0, entry);
			mycpu->gd_cnt.v_vm_faults++;
			if (curthread->td_lwp)
				++curthread->td_lwp->lwp_ru.ru_minflt;
			vm_page_wakeup(m);
		} else {
			vm_page_wakeup(m);
		}
	}
	vm_object_chain_release(object);
	vm_object_drop(object);
}

/*
 * Object can be held shared
 */
static void
vm_prefault_quick(pmap_t pmap, vm_offset_t addra,
		  vm_map_entry_t entry, int prot, int fault_flags)
{
	struct lwp *lp;
	vm_page_t m;
	vm_offset_t addr;
	vm_pindex_t pindex;
	vm_object_t object;
	int i;
	int noneg;
	int nopos;
	int maxpages;

	/*
	 * Get stable max count value, disabled if set to 0
	 */
	maxpages = vm_prefault_pages;
	cpu_ccfence();
	if (maxpages <= 0)
		return;

	/*
	 * We do not currently prefault mappings that use virtual page
	 * tables.  We do not prefault foreign pmaps.
	 */
	if (entry->maptype == VM_MAPTYPE_VPAGETABLE)
		return;
	lp = curthread->td_lwp;
	if (lp == NULL || (pmap != vmspace_pmap(lp->lwp_vmspace)))
		return;
	object = entry->object.vm_object;
	if (object->backing_object != NULL)
		return;
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));

	/*
	 * Limit pre-fault count to 1024 pages.
	 */
	if (maxpages > 1024)
		maxpages = 1024;

	noneg = 0;
	nopos = 0;
	for (i = 0; i < maxpages; ++i) {
		int error;

		/*
		 * Calculate the page to pre-fault, stopping the scan in
		 * each direction separately if the limit is reached.
		 */
		if (i & 1) {
			if (noneg)
				continue;
			addr = addra - ((i + 1) >> 1) * PAGE_SIZE;
		} else {
			if (nopos)
				continue;
			addr = addra + ((i + 2) >> 1) * PAGE_SIZE;
		}
		if (addr < entry->start) {
			noneg = 1;
			if (noneg && nopos)
				break;
			continue;
		}
		if (addr >= entry->end) {
			nopos = 1;
			if (noneg && nopos)
				break;
			continue;
		}

		/*
		 * Skip pages already mapped, and stop scanning in that
		 * direction.  When the scan terminates in both directions
		 * we are done.
		 */
		if (pmap_prefault_ok(pmap, addr) == 0) {
			if (i & 1)
				noneg = 1;
			else
				nopos = 1;
			if (noneg && nopos)
				break;
			continue;
		}

		/*
		 * Follow the VM object chain to obtain the page to be mapped
		 * into the pmap.  This version of the prefault code only
		 * works with terminal objects.
		 *
		 * WARNING!  We cannot call swap_pager_unswapped() with a
		 *	     shared token.
		 */
		pindex = ((addr - entry->start) + entry->offset) >> PAGE_SHIFT;

		m = vm_page_lookup_busy_try(object, pindex, TRUE, &error);
		if (m == NULL || error)
			continue;

		if (((m->valid & VM_PAGE_BITS_ALL) == VM_PAGE_BITS_ALL) &&
		    (m->flags & PG_FICTITIOUS) == 0 &&
		    ((m->flags & PG_SWAPPED) == 0 ||
		     (prot & VM_PROT_WRITE) == 0 ||
		     (fault_flags & VM_FAULT_DIRTY) == 0)) {
			/*
			 * A fully valid page not undergoing soft I/O can
			 * be immediately entered into the pmap.
			 */
			if ((m->queue - m->pc) == PQ_CACHE)
				vm_page_deactivate(m);
			if (prot & VM_PROT_WRITE) {
				vm_object_set_writeable_dirty(m->object);
				vm_set_nosync(m, entry);
				if (fault_flags & VM_FAULT_DIRTY) {
					vm_page_dirty(m);
					/*XXX*/
					swap_pager_unswapped(m);
				}
			}
			pmap_enter(pmap, addr, m, prot, 0, entry);
			mycpu->gd_cnt.v_vm_faults++;
			if (curthread->td_lwp)
				++curthread->td_lwp->lwp_ru.ru_minflt;
		}
		vm_page_wakeup(m);
	}
}
