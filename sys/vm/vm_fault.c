/*
 * Copyright (c) 2003-2020 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * ---
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
 * ---
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
#include <vm/swap_pager.h>
#include <vm/vm_extern.h>

#include <vm/vm_page2.h>

#define VM_FAULT_MAX_QUICK	16

struct faultstate {
	vm_page_t mary[VM_FAULT_MAX_QUICK];
	vm_map_backing_t ba;
	vm_prot_t prot;
	vm_page_t first_m;
	vm_map_backing_t first_ba;
	vm_prot_t first_prot;
	vm_map_t map;
	vm_map_entry_t entry;
	int lookup_still_valid;	/* 0=inv 1=valid/rel -1=valid/atomic */
	int hardfault;
	int fault_flags;
	int shared;
	int msoftonly;
	int first_shared;
	int wflags;
	int first_ba_held;	/* 0=unlocked 1=locked/rel -1=lock/atomic */
	struct vnode *vp;
};

__read_mostly static int debug_fault = 0;
SYSCTL_INT(_vm, OID_AUTO, debug_fault, CTLFLAG_RW, &debug_fault, 0, "");
__read_mostly static int debug_cluster = 0;
SYSCTL_INT(_vm, OID_AUTO, debug_cluster, CTLFLAG_RW, &debug_cluster, 0, "");
#if 0
static int virtual_copy_enable = 1;
SYSCTL_INT(_vm, OID_AUTO, virtual_copy_enable, CTLFLAG_RW,
		&virtual_copy_enable, 0, "");
#endif
__read_mostly int vm_shared_fault = 1;
TUNABLE_INT("vm.shared_fault", &vm_shared_fault);
SYSCTL_INT(_vm, OID_AUTO, shared_fault, CTLFLAG_RW,
		&vm_shared_fault, 0, "Allow shared token on vm_object");
__read_mostly static int vm_fault_bypass_count = 1;
TUNABLE_INT("vm.fault_bypass", &vm_fault_bypass_count);
SYSCTL_INT(_vm, OID_AUTO, fault_bypass, CTLFLAG_RW,
		&vm_fault_bypass_count, 0, "Allow fast vm_fault shortcut");

/*
 * Define here for debugging ioctls.  Note that these are globals, so
 * they were cause a ton of cache line bouncing.  Only use for debugging
 * purposes.
 */
/*#define VM_FAULT_QUICK_DEBUG */
#ifdef VM_FAULT_QUICK_DEBUG
static long vm_fault_bypass_success_count = 0;
SYSCTL_LONG(_vm, OID_AUTO, fault_bypass_success_count, CTLFLAG_RW,
		&vm_fault_bypass_success_count, 0, "");
static long vm_fault_bypass_failure_count1 = 0;
SYSCTL_LONG(_vm, OID_AUTO, fault_bypass_failure_count1, CTLFLAG_RW,
		&vm_fault_bypass_failure_count1, 0, "");
static long vm_fault_bypass_failure_count2 = 0;
SYSCTL_LONG(_vm, OID_AUTO, fault_bypass_failure_count2, CTLFLAG_RW,
		&vm_fault_bypass_failure_count2, 0, "");
static long vm_fault_bypass_failure_count3 = 0;
SYSCTL_LONG(_vm, OID_AUTO, fault_bypass_failure_count3, CTLFLAG_RW,
		&vm_fault_bypass_failure_count3, 0, "");
static long vm_fault_bypass_failure_count4 = 0;
SYSCTL_LONG(_vm, OID_AUTO, fault_bypass_failure_count4, CTLFLAG_RW,
		&vm_fault_bypass_failure_count4, 0, "");
#endif

static int vm_fault_bypass(struct faultstate *fs, vm_pindex_t first_pindex,
			vm_pindex_t first_count, int *mextcountp,
			vm_prot_t fault_type);
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
	vm_page_deactivate(fs->mary[0]);
	vm_page_wakeup(fs->mary[0]);
	fs->mary[0] = NULL;
}

static __inline void
unlock_map(struct faultstate *fs)
{
	if (fs->ba != fs->first_ba)
		vm_object_drop(fs->ba->object);
	if (fs->first_ba && fs->first_ba_held == 1) {
		vm_object_drop(fs->first_ba->object);
		fs->first_ba_held = 0;
		fs->first_ba = NULL;
	}
	fs->ba = NULL;

	/*
	 * NOTE: If lookup_still_valid == -1 the map is assumed to be locked
	 *	 and caller expects it to remain locked atomically.
	 */
	if (fs->lookup_still_valid == 1 && fs->map) {
		vm_map_lookup_done(fs->map, fs->entry, 0);
		fs->lookup_still_valid = 0;
		fs->entry = NULL;
	}
}

/*
 * Clean up after a successful call to vm_fault_object() so another call
 * to vm_fault_object() can be made.
 */
static void
cleanup_fault(struct faultstate *fs)
{
	/*
	 * We allocated a junk page for a COW operation that did
	 * not occur, the page must be freed.
	 */
	if (fs->ba != fs->first_ba) {
		KKASSERT(fs->first_shared == 0);

		/*
		 * first_m could be completely valid and we got here
		 * because of a PG_RAM, don't mistakenly free it!
		 */
		if ((fs->first_m->valid & VM_PAGE_BITS_ALL) ==
		    VM_PAGE_BITS_ALL) {
			vm_page_wakeup(fs->first_m);
		} else {
			vm_page_free(fs->first_m);
		}
		vm_object_pip_wakeup(fs->ba->object);
		fs->first_m = NULL;

		/*
		 * Reset fs->ba (used by vm_fault_vpagetahble() without
		 * calling unlock_map(), so we need a little duplication.
		 */
		vm_object_drop(fs->ba->object);
		fs->ba = fs->first_ba;
	}
}

static void
unlock_things(struct faultstate *fs)
{
	cleanup_fault(fs);
	unlock_map(fs);	
	if (fs->vp != NULL) { 
		vput(fs->vp);
		fs->vp = NULL;
	}
}

#if 0
/*
 * Virtual copy tests.   Used by the fault code to determine if a
 * page can be moved from an orphan vm_object into its shadow
 * instead of copying its contents.
 */
static __inline int
virtual_copy_test(struct faultstate *fs)
{
	/*
	 * Must be holding exclusive locks
	 */
	if (fs->first_shared || fs->shared || virtual_copy_enable == 0)
		return 0;

	/*
	 * Map, if present, has not changed
	 */
	if (fs->map && fs->map_generation != fs->map->timestamp)
		return 0;

	/*
	 * No refs, except us
	 */
	if (fs->ba->object->ref_count != 1)
		return 0;

	/*
	 * No one else can look this object up
	 */
	if (fs->ba->object->handle != NULL)
		return 0;

	/*
	 * No other ways to look the object up
	 */
	if (fs->ba->object->type != OBJT_DEFAULT &&
	    fs->ba->object->type != OBJT_SWAP)
		return 0;

	/*
	 * We don't chase down the shadow chain
	 */
	if (fs->ba != fs->first_ba->backing_ba)
		return 0;

	return 1;
}

static __inline int
virtual_copy_ok(struct faultstate *fs)
{
	if (virtual_copy_test(fs)) {
		/*
		 * Grab the lock and re-test changeable items.
		 */
		if (fs->lookup_still_valid == 0 && fs->map) {
			if (lockmgr(&fs->map->lock, LK_EXCLUSIVE|LK_NOWAIT))
				return 0;
			fs->lookup_still_valid = 1;
			if (virtual_copy_test(fs)) {
				fs->map_generation = ++fs->map->timestamp;
				return 1;
			}
			fs->lookup_still_valid = 0;
			lockmgr(&fs->map->lock, LK_RELEASE);
		}
	}
	return 0;
}
#endif

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
		(fs->ba->object->type != OBJT_DEFAULT &&		\
		(((fs->fault_flags & VM_FAULT_WIRE_MASK) == 0) ||	\
		 (fs->wflags & FW_WIRED)))

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
	vm_pindex_t first_pindex;
	vm_pindex_t first_count;
	struct faultstate fs;
	struct lwp *lp;
	struct proc *p;
	thread_t td;
	struct vm_map_ilock ilock;
	int mextcount;
	int didilock;
	int growstack;
	int retry = 0;
	int inherit_prot;
	int result;
	int n;

	inherit_prot = fault_type & VM_PROT_NOSYNC;
	fs.hardfault = 0;
	fs.fault_flags = fault_flags;
	fs.vp = NULL;
	fs.shared = vm_shared_fault;
	fs.first_shared = vm_shared_fault;
	growstack = 1;

	/*
	 * vm_map interactions
	 */
	td = curthread;
	if ((lp = td->td_lwp) != NULL)
		lp->lwp_flags |= LWP_PAGING;

RetryFault:
	/*
	 * vm_fault_bypass() can shortcut us.
	 */
	fs.msoftonly = 0;
	fs.first_ba_held = 0;
	mextcount = 1;

	/*
	 * Find the vm_map_entry representing the backing store and resolve
	 * the top level object and page index.  This may have the side
	 * effect of executing a copy-on-write on the map entry,
	 * creating a shadow object, or splitting an anonymous entry for
	 * performance, but will not COW any actual VM pages.
	 *
	 * On success fs.map is left read-locked and various other fields 
	 * are initialized but not otherwise referenced or locked.
	 *
	 * NOTE!  vm_map_lookup will try to upgrade the fault_type to
	 *	  VM_FAULT_WRITE if the map entry is a virtual page table
	 *	  and also writable, so we can set the 'A'accessed bit in
	 *	  the virtual page table entry.
	 */
	fs.map = map;
	result = vm_map_lookup(&fs.map, vaddr, fault_type,
			       &fs.entry, &fs.first_ba,
			       &first_pindex, &first_count,
			       &fs.first_prot, &fs.wflags);

	/*
	 * If the lookup failed or the map protections are incompatible,
	 * the fault generally fails.
	 *
	 * The failure could be due to TDF_NOFAULT if vm_map_lookup()
	 * tried to do a COW fault.
	 *
	 * If the caller is trying to do a user wiring we have more work
	 * to do.
	 */
	if (result != KERN_SUCCESS) {
		if (result == KERN_FAILURE_NOFAULT) {
			result = KERN_FAILURE;
			goto done;
		}
		if (result != KERN_PROTECTION_FAILURE ||
		    (fs.fault_flags & VM_FAULT_WIRE_MASK) != VM_FAULT_USER_WIRE)
		{
			if (result == KERN_INVALID_ADDRESS && growstack &&
			    map != &kernel_map && curproc != NULL) {
				result = vm_map_growstack(map, vaddr);
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
		 *
		 * XXX Try to allow the above by specifying OVERRIDE_WRITE.
		 */
		result = vm_map_lookup(&fs.map, vaddr,
				       VM_PROT_READ|VM_PROT_WRITE|
				        VM_PROT_OVERRIDE_WRITE,
				       &fs.entry, &fs.first_ba,
				       &first_pindex, &first_count,
				       &fs.first_prot, &fs.wflags);
		if (result != KERN_SUCCESS) {
			/* could also be KERN_FAILURE_NOFAULT */
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
		if ((fs.entry->protection & VM_PROT_WRITE) == 0) {
			atomic_clear_char(&fs.entry->max_protection,
					  VM_PROT_WRITE);
		}
	}

	/*
	 * fs.map is read-locked
	 *
	 * Misc checks.  Save the map generation number to detect races.
	 */
	fs.lookup_still_valid = 1;
	fs.first_m = NULL;
	fs.ba = fs.first_ba;		/* so unlock_things() works */
	fs.prot = fs.first_prot;	/* default (used by uksmap) */

	if (fs.entry->eflags & (MAP_ENTRY_NOFAULT | MAP_ENTRY_KSTACK)) {
		if (fs.entry->eflags & MAP_ENTRY_NOFAULT) {
			panic("vm_fault: fault on nofault entry, addr: %p",
			      (void *)vaddr);
		}
		if ((fs.entry->eflags & MAP_ENTRY_KSTACK) &&
		    vaddr >= fs.entry->ba.start &&
		    vaddr < fs.entry->ba.start + PAGE_SIZE) {
			panic("vm_fault: fault on stack guard, addr: %p",
			      (void *)vaddr);
		}
	}

	/*
	 * A user-kernel shared map has no VM object and bypasses
	 * everything.  We execute the uksmap function with a temporary
	 * fictitious vm_page.  The address is directly mapped with no
	 * management.
	 */
	if (fs.entry->maptype == VM_MAPTYPE_UKSMAP) {
		struct vm_page fakem;

		bzero(&fakem, sizeof(fakem));
		fakem.pindex = first_pindex;
		fakem.flags = PG_FICTITIOUS | PG_UNQUEUED;
		fakem.busy_count = PBUSY_LOCKED;
		fakem.valid = VM_PAGE_BITS_ALL;
		fakem.pat_mode = VM_MEMATTR_DEFAULT;
		if (fs.entry->ba.uksmap(&fs.entry->ba, UKSMAPOP_FAULT,
					fs.entry->aux.dev, &fakem)) {
			result = KERN_FAILURE;
			unlock_things(&fs);
			goto done2;
		}
		pmap_enter(fs.map->pmap, vaddr, &fakem, fs.prot | inherit_prot,
			   (fs.wflags & FW_WIRED), fs.entry);
		goto done_success;
	}

	/*
	 * A system map entry may return a NULL object.  No object means
	 * no pager means an unrecoverable kernel fault.
	 */
	if (fs.first_ba == NULL) {
		panic("vm_fault: unrecoverable fault at %p in entry %p",
			(void *)vaddr, fs.entry);
	}

	/*
	 * Fail here if not a trivial anonymous page fault and TDF_NOFAULT
	 * is set.
	 *
	 * Unfortunately a deadlock can occur if we are forced to page-in
	 * from swap, but diving all the way into the vm_pager_get_page()
	 * function to find out is too much.  Just check the object type.
	 *
	 * The deadlock is a CAM deadlock on a busy VM page when trying
	 * to finish an I/O if another process gets stuck in
	 * vop_helper_read_shortcut() due to a swap fault.
	 */
	if ((td->td_flags & TDF_NOFAULT) &&
	    (retry ||
	     fs.first_ba->object->type == OBJT_VNODE ||
	     fs.first_ba->object->type == OBJT_SWAP ||
	     fs.first_ba->backing_ba)) {
		result = KERN_FAILURE;
		unlock_things(&fs);
		goto done2;
	}

	/*
	 * If the entry is wired we cannot change the page protection.
	 */
	if (fs.wflags & FW_WIRED)
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
#if 0
	/* WORK IN PROGRESS, CODE REMOVED */
	if (fs.first_shared && fs.first_object->backing_object &&
	    LIST_EMPTY(&fs.first_object->shadow_head) &&
	    td->td_proc && td->td_proc->p_nthreads == 1) {
		fs.first_shared = 0;
	}
#endif

	/*
	 * VM_FAULT_UNSWAP - swap_pager_unswapped() needs an exclusive object
	 * VM_FAULT_DIRTY  - may require swap_pager_unswapped() later, but
	 *		     we can try shared first.
	 */
	if (fault_flags & VM_FAULT_UNSWAP)
		fs.first_shared = 0;

	/*
	 * Try to shortcut the entire mess and run the fault lockless.
	 * This will burst in multiple pages via fs->mary[].
	 */
	if (vm_fault_bypass_count &&
	    vm_fault_bypass(&fs, first_pindex, first_count,
			   &mextcount, fault_type) == KERN_SUCCESS) {
		didilock = 0;
		fault_flags &= ~VM_FAULT_BURST;
		goto success;
	}

	/*
	 * Exclusive heuristic (alloc page vs page exists)
	 */
	if (fs.first_ba->flags & VM_MAP_BACK_EXCL_HEUR)
		fs.first_shared = 0;

	/*
	 * Obtain a top-level object lock, shared or exclusive depending
	 * on fs.first_shared.  If a shared lock winds up being insufficient
	 * we will retry with an exclusive lock.
	 *
	 * The vnode pager lock is always shared.
	 */
	if (fs.first_shared)
		vm_object_hold_shared(fs.first_ba->object);
	else
		vm_object_hold(fs.first_ba->object);
	if (fs.vp == NULL)
		fs.vp = vnode_pager_lock(fs.first_ba);
	fs.first_ba_held = 1;

	/*
	 * The page we want is at (first_object, first_pindex), but if the
	 * vm_map_entry is VM_MAPTYPE_VPAGETABLE we have to traverse the
	 * page table to figure out the actual pindex.
	 *
	 * NOTE!  DEVELOPMENT IN PROGRESS, THIS IS AN INITIAL IMPLEMENTATION
	 * ONLY
	 */
	didilock = 0;
	if (fs.entry->maptype == VM_MAPTYPE_VPAGETABLE) {
		vm_map_interlock(fs.map, &ilock, vaddr, vaddr + PAGE_SIZE);
		didilock = 1;
		result = vm_fault_vpagetable(&fs, &first_pindex,
					     fs.entry->aux.master_pde,
					     fault_type, 1);
		if (result == KERN_TRY_AGAIN) {
			vm_map_deinterlock(fs.map, &ilock);
			++retry;
			goto RetryFault;
		}
		if (result != KERN_SUCCESS) {
			vm_map_deinterlock(fs.map, &ilock);
			goto done;
		}
	}

	/*
	 * Now we have the actual (object, pindex), fault in the page.  If
	 * vm_fault_object() fails it will unlock and deallocate the FS
	 * data.   If it succeeds everything remains locked and fs->ba->object
	 * will have an additional PIP count if fs->ba != fs->first_ba.
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

	if (debug_fault > 0) {
		--debug_fault;
		kprintf("VM_FAULT result %d addr=%jx type=%02x flags=%02x "
			"fs.m=%p fs.prot=%02x fs.wflags=%02x fs.entry=%p\n",
			result, (intmax_t)vaddr, fault_type, fault_flags,
			fs.mary[0], fs.prot, fs.wflags, fs.entry);
	}

	if (result == KERN_TRY_AGAIN) {
		if (didilock)
			vm_map_deinterlock(fs.map, &ilock);
		++retry;
		goto RetryFault;
	}
	if (result != KERN_SUCCESS) {
		if (didilock)
			vm_map_deinterlock(fs.map, &ilock);
		goto done;
	}

success:
	/*
	 * On success vm_fault_object() does not unlock or deallocate, and fs.m
	 * will contain a busied page.  It does drop fs->ba if appropriate.
	 *
	 * Enter the page into the pmap and do pmap-related adjustments.
	 *
	 * WARNING! Soft-busied fs.m's can only be manipulated in limited
	 *	    ways.
	 */
	KKASSERT(fs.lookup_still_valid != 0);
	vm_page_flag_set(fs.mary[0], PG_REFERENCED);

	for (n = 0; n < mextcount; ++n) {
		pmap_enter(fs.map->pmap, vaddr + (n << PAGE_SHIFT),
			   fs.mary[n], fs.prot | inherit_prot,
			   fs.wflags & FW_WIRED, fs.entry);
	}

	if (didilock)
		vm_map_deinterlock(fs.map, &ilock);

	/*
	 * If the page is not wired down, then put it where the pageout daemon
	 * can find it.
	 *
	 * NOTE: We cannot safely wire, unwire, or adjust queues for a
	 *	 soft-busied page.
	 */
	for (n = 0; n < mextcount; ++n) {
		if (fs.msoftonly) {
			KKASSERT(fs.mary[n]->busy_count & PBUSY_MASK);
			KKASSERT((fs.fault_flags & VM_FAULT_WIRE_MASK) == 0);
			vm_page_sbusy_drop(fs.mary[n]);
		} else {
			if (fs.fault_flags & VM_FAULT_WIRE_MASK) {
				if (fs.wflags & FW_WIRED)
					vm_page_wire(fs.mary[n]);
				else
					vm_page_unwire(fs.mary[n], 1);
			} else {
				vm_page_activate(fs.mary[n]);
			}
			KKASSERT(fs.mary[n]->busy_count & PBUSY_LOCKED);
			vm_page_wakeup(fs.mary[n]);
		}
	}

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
	    (fs.wflags & FW_WIRED) == 0) {
		if (fs.first_shared == 0 && fs.shared == 0) {
			vm_prefault(fs.map->pmap, vaddr,
				    fs.entry, fs.prot, fault_flags);
		} else {
			vm_prefault_quick(fs.map->pmap, vaddr,
					  fs.entry, fs.prot, fault_flags);
		}
	}

done_success:
	/*
	 * Unlock everything, and return
	 */
	unlock_things(&fs);

	mycpu->gd_cnt.v_vm_faults++;
	if (td->td_lwp) {
		if (fs.hardfault) {
			++td->td_lwp->lwp_ru.ru_majflt;
		} else {
			++td->td_lwp->lwp_ru.ru_minflt;
		}
	}

	/*vm_object_deallocate(fs.first_ba->object);*/
	/*fs.m = NULL; */

	result = KERN_SUCCESS;
done:
	if (fs.first_ba && fs.first_ba->object && fs.first_ba_held == 1) {
		vm_object_drop(fs.first_ba->object);
		fs.first_ba_held = 0;
	}
done2:
	if (lp)
		lp->lwp_flags &= ~LWP_PAGING;

#if !defined(NO_SWAPPING)
	/*
	 * Check the process RSS limit and force deactivation and
	 * (asynchronous) paging if necessary.  This is a complex operation,
	 * only do it for direct user-mode faults, for now.
	 *
	 * To reduce overhead implement approximately a ~16MB hysteresis.
	 */
	p = td->td_proc;
	if ((fault_flags & VM_FAULT_USERMODE) && lp &&
	    p->p_limit && map->pmap && vm_pageout_memuse_mode >= 1 &&
	    map != &kernel_map) {
		vm_pindex_t limit;
		vm_pindex_t size;

		limit = OFF_TO_IDX(qmin(p->p_rlimit[RLIMIT_RSS].rlim_cur,
					p->p_rlimit[RLIMIT_RSS].rlim_max));
		size = pmap_resident_tlnw_count(map->pmap);
		if (limit >= 0 && size > 4096 && size - 4096 >= limit) {
			vm_pageout_map_deactivate_pages(map, limit);
		}
	}
#endif

	if (result != KERN_SUCCESS && debug_fault < 0) {
		kprintf("VM_FAULT %d:%d (%s) result %d "
			"addr=%jx type=%02x flags=%02x "
			"fs.m=%p fs.prot=%02x fs.wflags=%02x fs.entry=%p\n",
			(curthread->td_proc ? curthread->td_proc->p_pid : -1),
			(curthread->td_lwp ? curthread->td_lwp->lwp_tid : -1),
			curthread->td_comm,
			result,
			(intmax_t)vaddr, fault_type, fault_flags,
			fs.mary[0], fs.prot, fs.wflags, fs.entry);
		while (debug_fault < 0 && (debug_fault & 1))
			tsleep(&debug_fault, 0, "DEBUG", hz);
	}

	return (result);
}

/*
 * Attempt a lockless vm_fault() shortcut.  The stars have to align for this
 * to work.  But if it does we can get our page only soft-busied and not
 * have to touch the vm_object or vnode locks at all.
 */
static
int
vm_fault_bypass(struct faultstate *fs, vm_pindex_t first_pindex,
	       vm_pindex_t first_count, int *mextcountp,
	       vm_prot_t fault_type)
{
	vm_page_t m;
	vm_object_t obj;	/* NOT LOCKED */
	int n;
	int nlim;

	/*
	 * Don't waste time if the object is only being used by one vm_map.
	 */
	obj = fs->first_ba->object;
#if 0
	if (obj->flags & OBJ_ONEMAPPING)
		return KERN_FAILURE;
#endif

	/*
	 * This will try to wire/unwire a page, which can't be done with
	 * a soft-busied page.
	 */
	if (fs->fault_flags & VM_FAULT_WIRE_MASK)
		return KERN_FAILURE;

	/*
	 * Ick, can't handle this
	 */
	if (fs->entry->maptype == VM_MAPTYPE_VPAGETABLE) {
#ifdef VM_FAULT_QUICK_DEBUG
		++vm_fault_bypass_failure_count1;
#endif
		return KERN_FAILURE;
	}

	/*
	 * Ok, try to get the vm_page quickly via the hash table.  The
	 * page will be soft-busied on success (NOT hard-busied).
	 */
	m = vm_page_hash_get(obj, first_pindex);
	if (m == NULL) {
#ifdef VM_FAULT_QUICK_DEBUG
		++vm_fault_bypass_failure_count2;
#endif
		return KERN_FAILURE;
	}
	if ((obj->flags & OBJ_DEAD) ||
	    m->valid != VM_PAGE_BITS_ALL ||
	    m->queue - m->pc != PQ_ACTIVE ||
	    (m->flags & PG_SWAPPED)) {
		vm_page_sbusy_drop(m);
#ifdef VM_FAULT_QUICK_DEBUG
		++vm_fault_bypass_failure_count3;
#endif
		return KERN_FAILURE;
	}

	/*
	 * The page is already fully valid, ACTIVE, and is not PG_SWAPPED.
	 *
	 * Don't map the page writable when emulating the dirty bit, a
	 * fault must be taken for proper emulation (vkernel).
	 */
	if (curthread->td_lwp && curthread->td_lwp->lwp_vmspace &&
	    pmap_emulate_ad_bits(&curthread->td_lwp->lwp_vmspace->vm_pmap)) {
		if ((fault_type & VM_PROT_WRITE) == 0)
			fs->prot &= ~VM_PROT_WRITE;
	}

	/*
	 * If this is a write fault the object and the page must already
	 * be writable.  Since we don't hold an object lock and only a
	 * soft-busy on the page, we cannot manipulate the object or
	 * the page state (other than the page queue).
	 */
	if (fs->prot & VM_PROT_WRITE) {
		if ((obj->flags & (OBJ_WRITEABLE | OBJ_MIGHTBEDIRTY)) !=
		    (OBJ_WRITEABLE | OBJ_MIGHTBEDIRTY) ||
		    m->dirty != VM_PAGE_BITS_ALL) {
			vm_page_sbusy_drop(m);
#ifdef VM_FAULT_QUICK_DEBUG
			++vm_fault_bypass_failure_count4;
#endif
			return KERN_FAILURE;
		}
		vm_set_nosync(m, fs->entry);
	}

	/*
	 * Set page and potentially burst in more
	 *
	 * Even though we are only soft-busied we can still move pages
	 * around in the normal queue(s).  The soft-busy prevents the
	 * page from being removed from the object, etc (normal operation).
	 *
	 * However, in this fast path it is excessively important to avoid
	 * any hard locks, so we use a special passive version of activate.
	 */
	fs->msoftonly = 1;
	fs->mary[0] = m;
	vm_page_soft_activate(m);

	if (vm_fault_bypass_count > 1) {
		nlim = vm_fault_bypass_count;
		if (nlim > VM_FAULT_MAX_QUICK)		/* array limit(+1) */
			nlim = VM_FAULT_MAX_QUICK;
		if (nlim > first_count)			/* user limit */
			nlim = first_count;

		for (n = 1; n < nlim; ++n) {
			m = vm_page_hash_get(obj, first_pindex + n);
			if (m == NULL)
				break;
			if (m->valid != VM_PAGE_BITS_ALL ||
			    m->queue - m->pc != PQ_ACTIVE ||
			    (m->flags & PG_SWAPPED)) {
				vm_page_sbusy_drop(m);
				break;
			}
			if (fs->prot & VM_PROT_WRITE) {
				if ((obj->flags & (OBJ_WRITEABLE |
						   OBJ_MIGHTBEDIRTY)) !=
				    (OBJ_WRITEABLE | OBJ_MIGHTBEDIRTY) ||
				    m->dirty != VM_PAGE_BITS_ALL) {
					vm_page_sbusy_drop(m);
					break;
				}
			}
			vm_page_soft_activate(m);
			fs->mary[n] = m;
		}
		*mextcountp = n;
	}

#ifdef VM_FAULT_QUICK_DEBUG
	++vm_fault_bypass_success_count;
#endif

	return KERN_SUCCESS;
}

/*
 * Fault in the specified virtual address in the current process map, 
 * returning a held VM page or NULL.  See vm_fault_page() for more 
 * information.
 *
 * No requirements.
 */
vm_page_t
vm_fault_page_quick(vm_offset_t va, vm_prot_t fault_type,
		    int *errorp, int *busyp)
{
	struct lwp *lp = curthread->td_lwp;
	vm_page_t m;

	m = vm_fault_page(&lp->lwp_vmspace->vm_map, va, 
			  fault_type, VM_FAULT_NORMAL,
			  errorp, busyp);
	return(m);
}

/*
 * Fault in the specified virtual address in the specified map, doing all
 * necessary manipulation of the object store and all necessary I/O.  Return
 * a held VM page or NULL, and set *errorp.  The related pmap is not
 * updated.
 *
 * If busyp is not NULL then *busyp will be set to TRUE if this routine
 * decides to return a busied page (aka VM_PROT_WRITE), or FALSE if it
 * does not (VM_PROT_WRITE not specified or busyp is NULL).  If busyp is
 * NULL the returned page is only held.
 *
 * If the caller has no intention of writing to the page's contents, busyp
 * can be passed as NULL along with VM_PROT_WRITE to force a COW operation
 * without busying the page.
 *
 * The returned page will also be marked PG_REFERENCED.
 *
 * If the page cannot be faulted writable and VM_PROT_WRITE was specified, an
 * error will be returned.
 *
 * No requirements.
 */
vm_page_t
vm_fault_page(vm_map_t map, vm_offset_t vaddr, vm_prot_t fault_type,
	      int fault_flags, int *errorp, int *busyp)
{
	vm_pindex_t first_pindex;
	vm_pindex_t first_count;
	struct faultstate fs;
	int result;
	int retry;
	int growstack;
	int didcow;
	vm_prot_t orig_fault_type = fault_type;

	retry = 0;
	didcow = 0;
	fs.hardfault = 0;
	fs.fault_flags = fault_flags;
	KKASSERT((fault_flags & VM_FAULT_WIRE_MASK) == 0);

	/*
	 * Dive the pmap (concurrency possible).  If we find the
	 * appropriate page we can terminate early and quickly.
	 *
	 * This works great for normal programs but will always return
	 * NULL for host lookups of vkernel maps in VMM mode.
	 *
	 * NOTE: pmap_fault_page_quick() might not busy the page.  If
	 *	 VM_PROT_WRITE is set in fault_type and pmap_fault_page_quick()
	 *	 returns non-NULL, it will safely dirty the returned vm_page_t
	 *	 for us.  We cannot safely dirty it here (it might not be
	 *	 busy).
	 */
	fs.mary[0] = pmap_fault_page_quick(map->pmap, vaddr, fault_type, busyp);
	if (fs.mary[0]) {
		*errorp = 0;
		return(fs.mary[0]);
	}

	/*
	 * Otherwise take a concurrency hit and do a formal page
	 * fault.
	 */
	fs.vp = NULL;
	fs.shared = vm_shared_fault;
	fs.first_shared = vm_shared_fault;
	fs.msoftonly = 0;
	growstack = 1;

	/*
	 * VM_FAULT_UNSWAP - swap_pager_unswapped() needs an exclusive object
	 * VM_FAULT_DIRTY  - may require swap_pager_unswapped() later, but
	 *		     we can try shared first.
	 */
	if (fault_flags & VM_FAULT_UNSWAP) {
		fs.first_shared = 0;
	}

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
	 *	  if the map entry is a virtual page table and also writable,
	 *	  so we can set the 'A'accessed bit in the virtual page table
	 *	  entry.
	 */
	fs.map = map;
	fs.first_ba_held = 0;
	result = vm_map_lookup(&fs.map, vaddr, fault_type,
			       &fs.entry, &fs.first_ba,
			       &first_pindex, &first_count,
			       &fs.first_prot, &fs.wflags);

	if (result != KERN_SUCCESS) {
		if (result == KERN_FAILURE_NOFAULT) {
			*errorp = KERN_FAILURE;
			fs.mary[0] = NULL;
			goto done;
		}
		if (result != KERN_PROTECTION_FAILURE ||
		    (fs.fault_flags & VM_FAULT_WIRE_MASK) != VM_FAULT_USER_WIRE)
		{
			if (result == KERN_INVALID_ADDRESS && growstack &&
			    map != &kernel_map && curproc != NULL) {
				result = vm_map_growstack(map, vaddr);
				if (result == KERN_SUCCESS) {
					growstack = 0;
					++retry;
					goto RetryFault;
				}
				result = KERN_FAILURE;
			}
			fs.mary[0] = NULL;
			*errorp = result;
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
				       &fs.entry, &fs.first_ba,
				       &first_pindex, &first_count,
				       &fs.first_prot, &fs.wflags);
		if (result != KERN_SUCCESS) {
			/* could also be KERN_FAILURE_NOFAULT */
			*errorp = KERN_FAILURE;
			fs.mary[0] = NULL;
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
		if ((fs.entry->protection & VM_PROT_WRITE) == 0) {
			atomic_clear_char(&fs.entry->max_protection,
					  VM_PROT_WRITE);
		}
	}

	/*
	 * fs.map is read-locked
	 *
	 * Misc checks.  Save the map generation number to detect races.
	 */
	fs.lookup_still_valid = 1;
	fs.first_m = NULL;
	fs.ba = fs.first_ba;

	if (fs.entry->eflags & MAP_ENTRY_NOFAULT) {
		panic("vm_fault: fault on nofault entry, addr: %lx",
		    (u_long)vaddr);
	}

	/*
	 * A user-kernel shared map has no VM object and bypasses
	 * everything.  We execute the uksmap function with a temporary
	 * fictitious vm_page.  The address is directly mapped with no
	 * management.
	 */
	if (fs.entry->maptype == VM_MAPTYPE_UKSMAP) {
		struct vm_page fakem;

		bzero(&fakem, sizeof(fakem));
		fakem.pindex = first_pindex;
		fakem.flags = PG_FICTITIOUS | PG_UNQUEUED;
		fakem.busy_count = PBUSY_LOCKED;
		fakem.valid = VM_PAGE_BITS_ALL;
		fakem.pat_mode = VM_MEMATTR_DEFAULT;
		if (fs.entry->ba.uksmap(&fs.entry->ba, UKSMAPOP_FAULT,
					fs.entry->aux.dev, &fakem)) {
			*errorp = KERN_FAILURE;
			fs.mary[0] = NULL;
			unlock_things(&fs);
			goto done2;
		}
		fs.mary[0] = PHYS_TO_VM_PAGE(fakem.phys_addr);
		vm_page_hold(fs.mary[0]);
		if (busyp)
			*busyp = 0;	/* don't need to busy R or W */
		unlock_things(&fs);
		*errorp = 0;
		goto done;
	}


	/*
	 * A system map entry may return a NULL object.  No object means
	 * no pager means an unrecoverable kernel fault.
	 */
	if (fs.first_ba == NULL) {
		panic("vm_fault: unrecoverable fault at %p in entry %p",
			(void *)vaddr, fs.entry);
	}

	/*
	 * Fail here if not a trivial anonymous page fault and TDF_NOFAULT
	 * is set.
	 *
	 * Unfortunately a deadlock can occur if we are forced to page-in
	 * from swap, but diving all the way into the vm_pager_get_page()
	 * function to find out is too much.  Just check the object type.
	 */
	if ((curthread->td_flags & TDF_NOFAULT) &&
	    (retry ||
	     fs.first_ba->object->type == OBJT_VNODE ||
	     fs.first_ba->object->type == OBJT_SWAP ||
	     fs.first_ba->backing_ba)) {
		*errorp = KERN_FAILURE;
		unlock_things(&fs);
		fs.mary[0] = NULL;
		goto done2;
	}

	/*
	 * If the entry is wired we cannot change the page protection.
	 */
	if (fs.wflags & FW_WIRED)
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
	if (fs.first_ba->flags & VM_MAP_BACK_EXCL_HEUR)
		fs.first_shared = 0;

	if (fs.first_shared)
		vm_object_hold_shared(fs.first_ba->object);
	else
		vm_object_hold(fs.first_ba->object);
	fs.first_ba_held = 1;
	if (fs.vp == NULL)
		fs.vp = vnode_pager_lock(fs.first_ba);	/* shared */

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
		first_count = 1;
		if (result == KERN_TRY_AGAIN) {
			++retry;
			goto RetryFault;
		}
		if (result != KERN_SUCCESS) {
			*errorp = result;
			fs.mary[0] = NULL;
			goto done;
		}
	}

	/*
	 * Now we have the actual (object, pindex), fault in the page.  If
	 * vm_fault_object() fails it will unlock and deallocate the FS
	 * data.   If it succeeds everything remains locked and fs->ba->object
	 * will have an additinal PIP count if fs->ba != fs->first_ba.
	 */
	fs.mary[0] = NULL;
	result = vm_fault_object(&fs, first_pindex, fault_type, 1);

	if (result == KERN_TRY_AGAIN) {
		KKASSERT(fs.first_ba_held == 0);
		++retry;
		didcow |= fs.wflags & FW_DIDCOW;
		goto RetryFault;
	}
	if (result != KERN_SUCCESS) {
		*errorp = result;
		fs.mary[0] = NULL;
		goto done;
	}

	if ((orig_fault_type & VM_PROT_WRITE) &&
	    (fs.prot & VM_PROT_WRITE) == 0) {
		*errorp = KERN_PROTECTION_FAILURE;
		unlock_things(&fs);
		fs.mary[0] = NULL;
		goto done;
	}

	/*
	 * Generally speaking we don't want to update the pmap because
	 * this routine can be called many times for situations that do
	 * not require updating the pmap, not to mention the page might
	 * already be in the pmap.
	 *
	 * However, if our vm_map_lookup() results in a COW, we need to
	 * at least remove the pte from the pmap to guarantee proper
	 * visibility of modifications made to the process.  For example,
	 * modifications made by vkernel uiocopy/related routines and
	 * modifications made by ptrace().
	 */
	vm_page_flag_set(fs.mary[0], PG_REFERENCED);
#if 0
	pmap_enter(fs.map->pmap, vaddr, fs.mary[0], fs.prot,
		   fs.wflags & FW_WIRED, NULL);
	mycpu->gd_cnt.v_vm_faults++;
	if (curthread->td_lwp)
		++curthread->td_lwp->lwp_ru.ru_minflt;
#endif
	if ((fs.wflags | didcow) | FW_DIDCOW) {
		pmap_remove(fs.map->pmap,
			    vaddr & ~PAGE_MASK,
			    (vaddr & ~PAGE_MASK) + PAGE_SIZE);
	}

	/*
	 * On success vm_fault_object() does not unlock or deallocate, and
	 * fs.mary[0] will contain a busied page.  So we must unlock here
	 * after having messed with the pmap.
	 */
	unlock_things(&fs);

	/*
	 * Return a held page.  We are not doing any pmap manipulation so do
	 * not set PG_MAPPED.  However, adjust the page flags according to
	 * the fault type because the caller may not use a managed pmapping
	 * (so we don't want to lose the fact that the page will be dirtied
	 * if a write fault was specified).
	 */
	if (fault_type & VM_PROT_WRITE)
		vm_page_dirty(fs.mary[0]);
	vm_page_activate(fs.mary[0]);

	if (curthread->td_lwp) {
		if (fs.hardfault) {
			curthread->td_lwp->lwp_ru.ru_majflt++;
		} else {
			curthread->td_lwp->lwp_ru.ru_minflt++;
		}
	}

	/*
	 * Unlock everything, and return the held or busied page.
	 */
	if (busyp) {
		if (fault_type & VM_PROT_WRITE) {
			vm_page_dirty(fs.mary[0]);
			*busyp = 1;
		} else {
			*busyp = 0;
			vm_page_hold(fs.mary[0]);
			vm_page_wakeup(fs.mary[0]);
		}
	} else {
		vm_page_hold(fs.mary[0]);
		vm_page_wakeup(fs.mary[0]);
	}
	/*vm_object_deallocate(fs.first_ba->object);*/
	*errorp = 0;

done:
	KKASSERT(fs.first_ba_held == 0);
done2:
	return(fs.mary[0]);
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
	vm_pindex_t first_count;
	struct faultstate fs;
	struct vm_map_entry entry;

	/*
	 * Since we aren't actually faulting the page into a
	 * pmap we can just fake the entry.ba.
	 */
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	bzero(&entry, sizeof(entry));
	entry.maptype = VM_MAPTYPE_NORMAL;
	entry.protection = entry.max_protection = fault_type;
	entry.ba.backing_ba = NULL;
	entry.ba.object = object;
	entry.ba.offset = 0;

	fs.hardfault = 0;
	fs.fault_flags = fault_flags;
	fs.map = NULL;
	fs.shared = vm_shared_fault;
	fs.first_shared = *sharedp;
	fs.msoftonly = 0;
	fs.vp = NULL;
	fs.first_ba_held = -1;	/* object held across call, prevent drop */
	KKASSERT((fault_flags & VM_FAULT_WIRE_MASK) == 0);

	/*
	 * VM_FAULT_UNSWAP - swap_pager_unswapped() needs an exclusive object
	 * VM_FAULT_DIRTY  - may require swap_pager_unswapped() later, but
	 *		     we can try shared first.
	 */
	if (fs.first_shared && (fault_flags & VM_FAULT_UNSWAP)) {
		fs.first_shared = 0;
		vm_object_upgrade(object);
	}

	/*
	 * Retry loop as needed (typically for shared->exclusive transitions)
	 */
RetryFault:
	*sharedp = fs.first_shared;
	first_pindex = OFF_TO_IDX(offset);
	first_count = 1;
	fs.first_ba = &entry.ba;
	fs.ba = fs.first_ba;
	fs.entry = &entry;
	fs.first_prot = fault_type;
	fs.wflags = 0;

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
		fs.vp = vnode_pager_lock(fs.first_ba);

	fs.lookup_still_valid = 1;
	fs.first_m = NULL;

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
	 * data.   If it succeeds everything remains locked and fs->ba->object
	 * will have an additinal PIP count if fs->ba != fs->first_ba.
	 *
	 * On KERN_TRY_AGAIN vm_fault_object() leaves fs.first_ba intact.
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
		unlock_things(&fs);
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
	vm_page_hold(fs.mary[0]);
	vm_page_activate(fs.mary[0]);
	if ((fault_type & VM_PROT_WRITE) || (fault_flags & VM_FAULT_DIRTY))
		vm_page_dirty(fs.mary[0]);
	if (fault_flags & VM_FAULT_UNSWAP)
		swap_pager_unswapped(fs.mary[0]);

	/*
	 * Indicate that the page was accessed.
	 */
	vm_page_flag_set(fs.mary[0], PG_REFERENCED);

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
	vm_page_wakeup(fs.mary[0]);
	/*vm_object_deallocate(fs.first_ba->object);*/

	*errorp = 0;
	return(fs.mary[0]);
}

/*
 * Translate the virtual page number (first_pindex) that is relative
 * to the address space into a logical page number that is relative to the
 * backing object.  Use the virtual page table pointed to by (vpte).
 *
 * Possibly downgrade the protection based on the vpte bits.
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
	int result;
	vpte_t *ptep;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(fs->first_ba->object));
	for (;;) {
		/*
		 * We cannot proceed if the vpte is not valid, not readable
		 * for a read fault, not writable for a write fault, or
		 * not executable for an instruction execution fault.
		 */
		if ((vpte & VPTE_V) == 0) {
			unlock_things(fs);
			return (KERN_FAILURE);
		}
		if ((fault_type & VM_PROT_WRITE) && (vpte & VPTE_RW) == 0) {
			unlock_things(fs);
			return (KERN_FAILURE);
		}
		if ((fault_type & VM_PROT_EXECUTE) && (vpte & VPTE_NX)) {
			unlock_things(fs);
			return (KERN_FAILURE);
		}
		if ((vpte & VPTE_PS) || vshift == 0)
			break;

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
		 * Process the returned fs.mary[0] and look up the page table
		 * entry in the page table page.
		 */
		vshift -= VPTE_PAGE_BITS;
		lwb = lwbuf_alloc(fs->mary[0], &lwb_cache);
		ptep = ((vpte_t *)lwbuf_kva(lwb) +
		        ((*pindex >> vshift) & VPTE_PAGE_MASK));
		vm_page_activate(fs->mary[0]);

		/*
		 * Page table write-back - entire operation including
		 * validation of the pte must be atomic to avoid races
		 * against the vkernel changing the pte.
		 *
		 * If the vpte is valid for the* requested operation, do
		 * a write-back to the page table.
		 *
		 * XXX VPTE_M is not set properly for page directory pages.
		 * It doesn't get set in the page directory if the page table
		 * is modified during a read access.
		 */
		for (;;) {
			vpte_t nvpte;

			/*
			 * Reload for the cmpset, but make sure the pte is
			 * still valid.
			 */
			vpte = *ptep;
			cpu_ccfence();
			nvpte = vpte;

			if ((vpte & VPTE_V) == 0)
				break;

			if ((fault_type & VM_PROT_WRITE) && (vpte & VPTE_RW))
				nvpte |= VPTE_M | VPTE_A;
			if (fault_type & (VM_PROT_READ | VM_PROT_EXECUTE))
				nvpte |= VPTE_A;
			if (vpte == nvpte)
				break;
			if (atomic_cmpset_long(ptep, vpte, nvpte)) {
				vm_page_dirty(fs->mary[0]);
				break;
			}
		}
		lwbuf_free(lwb);
		vm_page_flag_set(fs->mary[0], PG_REFERENCED);
		vm_page_wakeup(fs->mary[0]);
		fs->mary[0] = NULL;
		cleanup_fault(fs);
	}

	/*
	 * When the vkernel sets VPTE_RW it expects the real kernel to
	 * reflect VPTE_M back when the page is modified via the mapping.
	 * In order to accomplish this the real kernel must map the page
	 * read-only for read faults and use write faults to reflect VPTE_M
	 * back.
	 *
	 * Once VPTE_M has been set, the real kernel's pte allows writing.
	 * If the vkernel clears VPTE_M the vkernel must be sure to
	 * MADV_INVAL the real kernel's mappings to force the real kernel
	 * to re-fault on the next write so oit can set VPTE_M again.
	 */
	if ((fault_type & VM_PROT_WRITE) == 0 &&
	    (vpte & (VPTE_RW | VPTE_M)) != (VPTE_RW | VPTE_M)) {
		fs->first_prot &= ~VM_PROT_WRITE;
	}

	/*
	 * Disable EXECUTE perms if NX bit is set.
	 */
	if (vpte & VPTE_NX)
		fs->first_prot &= ~VM_PROT_EXECUTE;

	/*
	 * Combine remaining address bits with the vpte.
	 */
	*pindex = ((vpte & VPTE_FRAME) >> PAGE_SHIFT) +
		  (*pindex & ((1L << vshift) - 1));
	return (KERN_SUCCESS);
}


/*
 * This is the core of the vm_fault code.
 *
 * Do all operations required to fault-in (fs.first_ba->object, pindex).
 * Run through the backing store as necessary and do required COW or virtual
 * copy operations.  The caller has already fully resolved the vm_map_entry
 * and, if appropriate, has created a copy-on-write layer.  All we need to
 * do is iterate the object chain.
 *
 * On failure (fs) is unlocked and deallocated and the caller may return or
 * retry depending on the failure code.  On success (fs) is NOT unlocked or
 * deallocated, fs.mary[0] will contained a resolved, busied page, and fs.ba's
 * object will have an additional PIP count if it is not equal to
 * fs.first_ba.
 *
 * If locks based on fs->first_shared or fs->shared are insufficient,
 * clear the appropriate field(s) and return RETRY.  COWs require that
 * first_shared be 0, while page allocations (or frees) require that
 * shared be 0.  Renames require that both be 0.
 *
 * NOTE! fs->[first_]shared might be set with VM_FAULT_DIRTY also set.
 *	 we will have to retry with it exclusive if the vm_page is
 *	 PG_SWAPPED.
 *
 * fs->first_ba->object must be held on call.
 */
static
int
vm_fault_object(struct faultstate *fs, vm_pindex_t first_pindex,
		vm_prot_t fault_type, int allow_nofault)
{
	vm_map_backing_t next_ba;
	vm_pindex_t pindex;
	int error;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(fs->first_ba->object));
	fs->prot = fs->first_prot;
	pindex = first_pindex;
	KKASSERT(fs->ba == fs->first_ba);

	vm_object_pip_add(fs->first_ba->object, 1);

	/* 
	 * If a read fault occurs we try to upgrade the page protection
	 * and make it also writable if possible.  There are three cases
	 * where we cannot make the page mapping writable:
	 *
	 * (1) The mapping is read-only or the VM object is read-only,
	 *     fs->prot above will simply not have VM_PROT_WRITE set.
	 *
	 * (2) If the mapping is a virtual page table fs->first_prot will
	 *     have already been properly adjusted by vm_fault_vpagetable().
	 *     to detect writes so we can set VPTE_M in the virtual page
	 *     table.  Used by vkernels.
	 *
	 * (3) If the VM page is read-only or copy-on-write, upgrading would
	 *     just result in an unnecessary COW fault.
	 *
	 * (4) If the pmap specifically requests A/M bit emulation, downgrade
	 *     here.
	 */
#if 0
	/* see vpagetable code */
	if (fs->entry->maptype == VM_MAPTYPE_VPAGETABLE) {
		if ((fault_type & VM_PROT_WRITE) == 0)
			fs->prot &= ~VM_PROT_WRITE;
	}
#endif

	if (curthread->td_lwp && curthread->td_lwp->lwp_vmspace &&
	    pmap_emulate_ad_bits(&curthread->td_lwp->lwp_vmspace->vm_pmap)) {
		if ((fault_type & VM_PROT_WRITE) == 0)
			fs->prot &= ~VM_PROT_WRITE;
	}

	/* vm_object_hold(fs->ba->object); implied b/c ba == first_ba */

	for (;;) {
		/*
		 * If the object is dead, we stop here
		 */
		if (fs->ba->object->flags & OBJ_DEAD) {
			vm_object_pip_wakeup(fs->first_ba->object);
			unlock_things(fs);
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
		fs->mary[0] = vm_page_lookup_busy_try(fs->ba->object, pindex,
						      TRUE, &error);
		if (error) {
			vm_object_pip_wakeup(fs->first_ba->object);
			unlock_things(fs);
			vm_page_sleep_busy(fs->mary[0], TRUE, "vmpfw");
			mycpu->gd_cnt.v_intrans++;
			fs->mary[0] = NULL;
			return (KERN_TRY_AGAIN);
		}
		if (fs->mary[0]) {
			/*
			 * The page is busied for us.
			 *
			 * If reactivating a page from PQ_CACHE we may have
			 * to rate-limit.
			 */
			int queue = fs->mary[0]->queue;
			vm_page_unqueue_nowakeup(fs->mary[0]);

			if ((queue - fs->mary[0]->pc) == PQ_CACHE &&
			    vm_page_count_severe()) {
				vm_page_activate(fs->mary[0]);
				vm_page_wakeup(fs->mary[0]);
				fs->mary[0] = NULL;
				vm_object_pip_wakeup(fs->first_ba->object);
				unlock_things(fs);
				if (allow_nofault == 0 ||
				    (curthread->td_flags & TDF_NOFAULT) == 0) {
					thread_t td;

					vm_wait_pfault();
					td = curthread;
					if (td->td_proc && (td->td_proc->p_flags & P_LOWMEMKILL))
						return (KERN_PROTECTION_FAILURE);
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
			if (fs->mary[0]->object != &kernel_object) {
				if ((fs->mary[0]->valid & VM_PAGE_BITS_ALL) !=
				    VM_PAGE_BITS_ALL) {
					goto readrest;
				}
				if (fs->mary[0]->flags & PG_RAM) {
					if (debug_cluster)
						kprintf("R");
					vm_page_flag_clear(fs->mary[0], PG_RAM);
					goto readrest;
				}
			}
			atomic_clear_int(&fs->first_ba->flags,
					 VM_MAP_BACK_EXCL_HEUR);
			break; /* break to PAGE HAS BEEN FOUND */
		}

		/*
		 * Page is not resident, If this is the search termination
		 * or the pager might contain the page, allocate a new page.
		 */
		if (TRYPAGER(fs) || fs->ba == fs->first_ba) {
			/*
			 * If this is a SWAP object we can use the shared
			 * lock to check existence of a swap block.  If
			 * there isn't one we can skip to the next object.
			 *
			 * However, if this is the first object we allocate
			 * a page now just in case we need to copy to it
			 * later.
			 */
			if (fs->ba != fs->first_ba &&
			    fs->ba->object->type == OBJT_SWAP) {
				if (swap_pager_haspage_locked(fs->ba->object,
							      pindex) == 0) {
					goto next;
				}
			}

			/*
			 * Allocating, must be exclusive.
			 */
			atomic_set_int(&fs->first_ba->flags,
				       VM_MAP_BACK_EXCL_HEUR);
			if (fs->ba == fs->first_ba && fs->first_shared) {
				fs->first_shared = 0;
				vm_object_pip_wakeup(fs->first_ba->object);
				unlock_things(fs);
				return (KERN_TRY_AGAIN);
			}
			if (fs->ba != fs->first_ba && fs->shared) {
				fs->first_shared = 0;
				fs->shared = 0;
				vm_object_pip_wakeup(fs->first_ba->object);
				unlock_things(fs);
				return (KERN_TRY_AGAIN);
			}

			/*
			 * If the page is beyond the object size we fail
			 */
			if (pindex >= fs->ba->object->size) {
				vm_object_pip_wakeup(fs->first_ba->object);
				unlock_things(fs);
				return (KERN_PROTECTION_FAILURE);
			}

			/*
			 * Allocate a new page for this object/offset pair.
			 *
			 * It is possible for the allocation to race, so
			 * handle the case.
			 */
			fs->mary[0] = NULL;
			if (!vm_page_count_severe()) {
				fs->mary[0] = vm_page_alloc(fs->ba->object,
				    pindex,
				    ((fs->vp || fs->ba->backing_ba) ?
					VM_ALLOC_NULL_OK | VM_ALLOC_NORMAL :
					VM_ALLOC_NULL_OK | VM_ALLOC_NORMAL |
					VM_ALLOC_USE_GD | VM_ALLOC_ZERO));
			}
			if (fs->mary[0] == NULL) {
				vm_object_pip_wakeup(fs->first_ba->object);
				unlock_things(fs);
				if (allow_nofault == 0 ||
				    (curthread->td_flags & TDF_NOFAULT) == 0) {
					thread_t td;

					vm_wait_pfault();
					td = curthread;
					if (td->td_proc && (td->td_proc->p_flags & P_LOWMEMKILL))
						return (KERN_PROTECTION_FAILURE);
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
		 * If TRYPAGER is true then fs.mary[0] will be non-NULL and
		 * busied for us.
		 */
		if (TRYPAGER(fs)) {
			u_char behavior = vm_map_entry_behavior(fs->entry);
			vm_object_t object;
			vm_page_t first_m;
			int seqaccess;
			int rv;

			if (behavior == MAP_ENTRY_BEHAV_RANDOM)
				seqaccess = 0;
			else
				seqaccess = -1;

			/*
			 * Doing I/O may synchronously insert additional
			 * pages so we can't be shared at this point either.
			 *
			 * NOTE: We can't free fs->mary[0] here in the
			 *	 allocated case (fs->ba != fs->first_ba) as
			 *	 this would require an exclusively locked
			 *	 VM object.
			 */
			if (fs->ba == fs->first_ba && fs->first_shared) {
				vm_page_deactivate(fs->mary[0]);
				vm_page_wakeup(fs->mary[0]);
				fs->mary[0]= NULL;
				fs->first_shared = 0;
				vm_object_pip_wakeup(fs->first_ba->object);
				unlock_things(fs);
				return (KERN_TRY_AGAIN);
			}
			if (fs->ba != fs->first_ba && fs->shared) {
				vm_page_deactivate(fs->mary[0]);
				vm_page_wakeup(fs->mary[0]);
				fs->mary[0] = NULL;
				fs->first_shared = 0;
				fs->shared = 0;
				vm_object_pip_wakeup(fs->first_ba->object);
				unlock_things(fs);
				return (KERN_TRY_AGAIN);
			}

			object = fs->ba->object;
			first_m = NULL;

			/* object is held, no more access to entry or ba's */

			/*
			 * Acquire the page data.  We still hold object
			 * and the page has been BUSY's.
			 *
			 * We own the page, but we must re-issue the lookup
			 * because the pager may have replaced it (for example,
			 * in order to enter a fictitious page into the
			 * object).  In this situation the pager will have
			 * cleaned up the old page and left the new one
			 * busy for us.
			 *
			 * If we got here through a PG_RAM read-ahead
			 * mark the page may be partially dirty and thus
			 * not freeable.  Don't bother checking to see
			 * if the pager has the page because we can't free
			 * it anyway.  We have to depend on the get_page
			 * operation filling in any gaps whether there is
			 * backing store or not.
			 *
			 * We must dispose of the page (fs->mary[0]) and also
			 * possibly first_m (the fronting layer).  If
			 * this is a write fault leave the page intact
			 * because we will probably have to copy fs->mary[0]
			 * to fs->first_m on the retry.  If this is a
			 * read fault we probably won't need the page.
			 */
			rv = vm_pager_get_page(object, &fs->mary[0], seqaccess);

			if (rv == VM_PAGER_OK) {
				++fs->hardfault;
				fs->mary[0] = vm_page_lookup(object, pindex);
				if (fs->mary[0]) {
					vm_page_activate(fs->mary[0]);
					vm_page_wakeup(fs->mary[0]);
					fs->mary[0] = NULL;
				}

				if (fs->mary[0]) {
					/* have page */
					break;
				}
				vm_object_pip_wakeup(fs->first_ba->object);
				unlock_things(fs);
				return (KERN_TRY_AGAIN);
			}

			/*
			 * If the pager doesn't have the page, continue on
			 * to the next object.  Retain the vm_page if this
			 * is the first object, we may need to copy into
			 * it later.
			 */
			if (rv == VM_PAGER_FAIL) {
				if (fs->ba != fs->first_ba) {
					vm_page_free(fs->mary[0]);
					fs->mary[0] = NULL;
				}
				goto next;
			}

			/*
			 * Remove the bogus page (which does not exist at this
			 * object/offset).
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
						curthread->td_comm);
				}
			}

			/*
			 * I/O error or data outside pager's range.
			 */
			if (fs->mary[0]) {
				vnode_pager_freepage(fs->mary[0]);
				fs->mary[0] = NULL;
			}
			if (first_m) {
				vm_page_free(first_m);
				first_m = NULL;		/* safety */
			}
			vm_object_pip_wakeup(object);
			unlock_things(fs);

			switch(rv) {
			case VM_PAGER_ERROR:
				return (KERN_FAILURE);
			case VM_PAGER_BAD:
				return (KERN_PROTECTION_FAILURE);
			default:
				return (KERN_PROTECTION_FAILURE);
			}

#if 0
			/*
			 * Data outside the range of the pager or an I/O error
			 *
			 * The page may have been wired during the pagein,
			 * e.g. by the buffer cache, and cannot simply be
			 * freed.  Call vnode_pager_freepage() to deal with it.
			 *
			 * The object is not held shared so we can safely
			 * free the page.
			 */
			if (fs->ba != fs->first_ba) {

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
					/* from just above */
					KKASSERT(fs->first_shared == 0);
					vnode_pager_freepage(fs->m);
					fs->m = NULL;
				}
				/* NOT REACHED */
			}
#endif
		}

next:
		/*
		 * We get here if the object has a default pager (or unwiring) 
		 * or the pager doesn't have the page.
		 *
		 * fs->first_m will be used for the COW unless we find a
		 * deeper page to be mapped read-only, in which case the
		 * unlock*(fs) will free first_m.
		 */
		if (fs->ba == fs->first_ba)
			fs->first_m = fs->mary[0];

		/*
		 * Move on to the next object.  The chain lock should prevent
		 * the backing_object from getting ripped out from under us.
		 *
		 * The object lock for the next object is governed by
		 * fs->shared.
		 */
		next_ba = fs->ba->backing_ba;
		if (next_ba == NULL) {
			/*
			 * If there's no object left, fill the page in the top
			 * object with zeros.
			 */
			if (fs->ba != fs->first_ba) {
				vm_object_pip_wakeup(fs->ba->object);
				vm_object_drop(fs->ba->object);
				fs->ba = fs->first_ba;
				pindex = first_pindex;
				fs->mary[0] = fs->first_m;
			}
			fs->first_m = NULL;

			/*
			 * Zero the page and mark it valid.
			 */
			vm_page_zero_fill(fs->mary[0]);
			mycpu->gd_cnt.v_zfod++;
			fs->mary[0]->valid = VM_PAGE_BITS_ALL;
			break;	/* break to PAGE HAS BEEN FOUND */
		}

		if (fs->shared)
			vm_object_hold_shared(next_ba->object);
		else
			vm_object_hold(next_ba->object);
		KKASSERT(next_ba == fs->ba->backing_ba);
		pindex -= OFF_TO_IDX(fs->ba->offset);
		pindex += OFF_TO_IDX(next_ba->offset);

		if (fs->ba != fs->first_ba) {
			vm_object_pip_wakeup(fs->ba->object);
			vm_object_lock_swap();	/* flip ba/next_ba */
			vm_object_drop(fs->ba->object);
		}
		fs->ba = next_ba;
		vm_object_pip_add(next_ba->object, 1);
	}

	/*
	 * PAGE HAS BEEN FOUND. [Loop invariant still holds -- the object lock
	 * is held.]
	 *
	 * object still held.
	 * vm_map may not be locked (determined by fs->lookup_still_valid)
	 *
	 * local shared variable may be different from fs->shared.
	 *
	 * If the page is being written, but isn't already owned by the
	 * top-level object, we have to copy it into a new page owned by the
	 * top-level object.
	 */
	KASSERT((fs->mary[0]->busy_count & PBUSY_LOCKED) != 0,
		("vm_fault: not busy after main loop"));

	if (fs->ba != fs->first_ba) {
		/*
		 * We only really need to copy if we want to write it.
		 */
		if (fault_type & VM_PROT_WRITE) {
#if 0
			/* CODE REFACTOR IN PROGRESS, REMOVE OPTIMIZATION */
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
			if (virtual_copy_ok(fs)) {
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
				vm_page_protect(fs->first_m, VM_PROT_NONE);
				vm_page_remove(fs->first_m);
				vm_page_rename(fs->mary[0],
					       fs->first_ba->object,
					       first_pindex);
				vm_page_free(fs->first_m);
				fs->first_m = fs->mary[0];
				fs->mary[0] = NULL;
				mycpu->gd_cnt.v_cow_optim++;
			} else
#endif
			{
				/*
				 * Oh, well, lets copy it.
				 *
				 * We used to unmap the original page here
				 * because vm_fault_page() didn't and this
				 * would cause havoc for the umtx*() code
				 * and the procfs code.
				 *
				 * This is no longer necessary.  The
				 * vm_fault_page() routine will now unmap the
				 * page after a COW, and the umtx code will
				 * recover on its own.
				 */
				/*
				 * NOTE: Since fs->mary[0] is a backing page,
				 *	 it is read-only, so there isn't any
				 *	 copy race vs writers.
				 */
				KKASSERT(fs->first_shared == 0);
				vm_page_copy(fs->mary[0], fs->first_m);
				/* pmap_remove_specific(
				    &curthread->td_lwp->lwp_vmspace->vm_pmap,
				    fs->mary[0]); */
			}

			/*
			 * We no longer need the old page or object.
			 */
			if (fs->mary[0])
				release_page(fs);

			/*
			 * fs->ba != fs->first_ba due to above conditional
			 */
			vm_object_pip_wakeup(fs->ba->object);
			vm_object_drop(fs->ba->object);
			fs->ba = fs->first_ba;

			/*
			 * Only use the new page below...
			 */
			mycpu->gd_cnt.v_cow_faults++;
			fs->mary[0] = fs->first_m;
			pindex = first_pindex;
		} else {
			/*
			 * If it wasn't a write fault avoid having to copy
			 * the page by mapping it read-only from backing
			 * store.  The process is not allowed to modify
			 * backing pages.
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
	KKASSERT(fs->lookup_still_valid != 0);
#if 0
	if (fs->lookup_still_valid == 0 && fs->map) {
		if (relock_map(fs) ||
		    fs->map->timestamp != fs->map_generation) {
			release_page(fs);
			vm_object_pip_wakeup(fs->first_ba->object);
			unlock_things(fs);
			return (KERN_TRY_AGAIN);
		}
	}
#endif

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
	vm_page_activate(fs->mary[0]);
	if (fs->prot & VM_PROT_WRITE) {
		vm_object_set_writeable_dirty(fs->mary[0]->object);
		vm_set_nosync(fs->mary[0], fs->entry);
		if (fs->fault_flags & VM_FAULT_DIRTY) {
			vm_page_dirty(fs->mary[0]);
			if (fs->mary[0]->flags & PG_SWAPPED) {
				/*
				 * If the page is swapped out we have to call
				 * swap_pager_unswapped() which requires an
				 * exclusive object lock.  If we are shared,
				 * we must clear the shared flag and retry.
				 */
				if ((fs->ba == fs->first_ba &&
				     fs->first_shared) ||
				    (fs->ba != fs->first_ba && fs->shared)) {
					vm_page_wakeup(fs->mary[0]);
					fs->mary[0] = NULL;
					if (fs->ba == fs->first_ba)
						fs->first_shared = 0;
					else
						fs->shared = 0;
					vm_object_pip_wakeup(
							fs->first_ba->object);
					unlock_things(fs);
					return (KERN_TRY_AGAIN);
				}
				swap_pager_unswapped(fs->mary[0]);
			}
		}
	}

	/*
	 * We found our page at backing layer ba.  Leave the layer state
	 * intact.
	 */

	vm_object_pip_wakeup(fs->first_ba->object);
#if 0
	if (fs->ba != fs->first_ba)
		vm_object_drop(fs->ba->object);
#endif

	/*
	 * Page had better still be busy.  We are still locked up and 
	 * fs->ba->object will have another PIP reference for the case
	 * where fs->ba != fs->first_ba.
	 */
	KASSERT(fs->mary[0]->busy_count & PBUSY_LOCKED,
		("vm_fault: page %p not busy!", fs->mary[0]));

	/*
	 * Sanity check: page must be completely valid or it is not fit to
	 * map into user space.  vm_pager_get_pages() ensures this.
	 */
	if (fs->mary[0]->valid != VM_PAGE_BITS_ALL) {
		vm_page_zero_invalid(fs->mary[0], TRUE);
		kprintf("Warning: page %p partially invalid on fault\n",
			fs->mary[0]);
	}

	return (KERN_SUCCESS);
}

/*
 * Wire down a range of virtual addresses in a map.  The entry in question
 * should be marked in-transition and the map must be locked.  We must
 * release the map temporarily while faulting-in the page to avoid a
 * deadlock.  Note that the entry may be clipped while we are blocked but
 * will never be freed.
 *
 * map must be locked on entry.
 */
int
vm_fault_wire(vm_map_t map, vm_map_entry_t entry,
	      boolean_t user_wire, int kmflags)
{
	boolean_t fictitious;
	vm_offset_t start;
	vm_offset_t end;
	vm_offset_t va;
	pmap_t pmap;
	int rv;
	int wire_prot;
	int fault_flags;
	vm_page_t m;

	if (user_wire) {
		wire_prot = VM_PROT_READ;
		fault_flags = VM_FAULT_USER_WIRE;
	} else {
		wire_prot = VM_PROT_READ | VM_PROT_WRITE;
		fault_flags = VM_FAULT_CHANGE_WIRING;
	}
	if (kmflags & KM_NOTLBSYNC)
		wire_prot |= VM_PROT_NOSYNC;

	pmap = vm_map_pmap(map);
	start = entry->ba.start;
	end = entry->ba.end;

	switch(entry->maptype) {
	case VM_MAPTYPE_NORMAL:
	case VM_MAPTYPE_VPAGETABLE:
		fictitious = entry->ba.object &&
			    ((entry->ba.object->type == OBJT_DEVICE) ||
			     (entry->ba.object->type == OBJT_MGTDEVICE));
		break;
	case VM_MAPTYPE_UKSMAP:
		fictitious = TRUE;
		break;
	default:
		fictitious = FALSE;
		break;
	}

	if (entry->eflags & MAP_ENTRY_KSTACK)
		start += PAGE_SIZE;
	map->timestamp++;
	vm_map_unlock(map);

	/*
	 * We simulate a fault to get the page and enter it in the physical
	 * map.
	 */
	for (va = start; va < end; va += PAGE_SIZE) {
		rv = vm_fault(map, va, wire_prot, fault_flags);
		if (rv) {
			while (va > start) {
				va -= PAGE_SIZE;
				m = pmap_unwire(pmap, va);
				if (m && !fictitious) {
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
	pmap_t pmap;
	vm_page_t m;

	pmap = vm_map_pmap(map);
	start = entry->ba.start;
	end = entry->ba.end;
	fictitious = entry->ba.object &&
			((entry->ba.object->type == OBJT_DEVICE) ||
			 (entry->ba.object->type == OBJT_MGTDEVICE));
	if (entry->eflags & MAP_ENTRY_KSTACK)
		start += PAGE_SIZE;

	/*
	 * Since the pages are wired down, we must be able to get their
	 * mappings from the physical map system.
	 */
	for (va = start; va < end; va += PAGE_SIZE) {
		m = pmap_unwire(pmap, va);
		if (m && !fictitious) {
			vm_page_busy_wait(m, FALSE, "vmwrpg");
			vm_page_unwire(m, 1);
			vm_page_wakeup(m);
		}
	}
}

/*
 * Simulate write faults to bring all data into the head object, return
 * KERN_SUCCESS on success (which should be always unless the system runs
 * out of memory).
 *
 * The caller will handle destroying the backing_ba's.
 */
int
vm_fault_collapse(vm_map_t map, vm_map_entry_t entry)
{
	struct faultstate fs;
	vm_ooffset_t scan;
	vm_pindex_t pindex;
	vm_object_t object;
	int rv;
	int all_shadowed;

	bzero(&fs, sizeof(fs));
	object = entry->ba.object;

	fs.first_prot = entry->max_protection |	/* optional VM_PROT_EXECUTE */
			VM_PROT_READ | VM_PROT_WRITE | VM_PROT_OVERRIDE_WRITE;
	fs.fault_flags = VM_FAULT_NORMAL;
	fs.map = map;
	fs.entry = entry;
	fs.lookup_still_valid = -1;	/* leave map atomically locked */
	fs.first_ba = &entry->ba;
	fs.first_ba_held = -1;		/* leave object held */

	/* fs.hardfault */

	vm_object_hold(object);
	rv = KERN_SUCCESS;

	scan = entry->ba.start;
	all_shadowed = 1;

	while (scan < entry->ba.end) {
		pindex = OFF_TO_IDX(entry->ba.offset + (scan - entry->ba.start));

		if (vm_page_lookup(object, pindex)) {
			scan += PAGE_SIZE;
			continue;
		}

		all_shadowed = 0;
		fs.ba = fs.first_ba;
		fs.prot = fs.first_prot;

		rv = vm_fault_object(&fs, pindex, fs.first_prot, 1);
		if (rv == KERN_TRY_AGAIN)
			continue;
		if (rv != KERN_SUCCESS)
			break;
		vm_page_flag_set(fs.mary[0], PG_REFERENCED);
		vm_page_activate(fs.mary[0]);
		vm_page_wakeup(fs.mary[0]);
		scan += PAGE_SIZE;
	}
	KKASSERT(entry->ba.object == object);
	vm_object_drop(object);

	/*
	 * If the fronting object did not have every page we have to clear
	 * the pmap range due to the pages being changed so we can fault-in
	 * the proper pages.
	 */
	if (all_shadowed == 0)
		pmap_remove(map->pmap, entry->ba.start, entry->ba.end);

	return rv;
}

/*
 * Copy all of the pages from one map entry to another.  If the source
 * is wired down we just use vm_page_lookup().  If not we use
 * vm_fault_object().
 *
 * The source and destination maps must be locked for write.
 * The source and destination maps token must be held
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

	src_object = src_entry->ba.object;
	src_offset = src_entry->ba.offset;

	/*
	 * Create the top-level object for the destination entry. (Doesn't
	 * actually shadow anything - we copy the pages directly.)
	 */
	vm_map_entry_allocate_object(dst_entry);
	dst_object = dst_entry->ba.object;

	prot = dst_entry->max_protection;

	/*
	 * Loop through all of the pages in the entry's range, copying each
	 * one from the source object (it should be there) to the destination
	 * object.
	 */
	vm_object_hold(src_object);
	vm_object_hold(dst_object);

	for (vaddr = dst_entry->ba.start, dst_offset = 0;
	     vaddr < dst_entry->ba.end;
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

		/*
		 * Enter it in the pmap...
		 */
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
__read_mostly static int vm_prefault_pages = 8;
SYSCTL_INT(_vm, OID_AUTO, prefault_pages, CTLFLAG_RW, &vm_prefault_pages, 0,
	   "Maximum number of pages to pre-fault");
__read_mostly static int vm_fast_fault = 1;
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
	vm_map_backing_t ba;	/* first ba */
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
	if (entry->maptype != VM_MAPTYPE_NORMAL)
		return;
	lp = curthread->td_lwp;
	if (lp == NULL || (pmap != vmspace_pmap(lp->lwp_vmspace)))
		return;

	/*
	 * Limit pre-fault count to 1024 pages.
	 */
	if (maxpages > 1024)
		maxpages = 1024;

	ba = &entry->ba;
	object = entry->ba.object;
	KKASSERT(object != NULL);

	/*
	 * NOTE: VM_FAULT_DIRTY allowed later so must hold object exclusively
	 *	 now (or do something more complex XXX).
	 */
	vm_object_hold(object);

	noneg = 0;
	nopos = 0;
	for (i = 0; i < maxpages; ++i) {
		vm_object_t lobject;
		vm_object_t nobject;
		vm_map_backing_t last_ba;	/* last ba */
		vm_map_backing_t next_ba;	/* last ba */
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
		if (addr < entry->ba.start) {
			noneg = 1;
			if (noneg && nopos)
				break;
			continue;
		}
		if (addr >= entry->ba.end) {
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
		 * Follow the backing layers to obtain the page to be mapped
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
		index = ((addr - entry->ba.start) + entry->ba.offset) >>
			PAGE_SHIFT;
		last_ba = ba;
		lobject = object;
		pindex = index;
		pprot = prot;

		/*vm_object_hold(lobject); implied */

		while ((m = vm_page_lookup_busy_try(lobject, pindex,
						    TRUE, &error)) == NULL) {
			if (lobject->type != OBJT_DEFAULT)
				break;
			if ((next_ba = last_ba->backing_ba) == NULL) {
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
			if (next_ba->offset & PAGE_MASK)
				break;
			nobject = next_ba->object;
			vm_object_hold(nobject);
			pindex -= last_ba->offset >> PAGE_SHIFT;
			pindex += next_ba->offset >> PAGE_SHIFT;
			if (last_ba != ba) {
				vm_object_lock_swap();
				vm_object_drop(lobject);
			}
			lobject = nobject;
			last_ba = next_ba;
			pprot &= ~VM_PROT_WRITE;
		}

		/*
		 * NOTE: A non-NULL (m) will be associated with lobject if
		 *	 it was found there, otherwise it is probably a
		 *	 zero-fill page associated with the base object.
		 *
		 * Give-up if no page is available.
		 */
		if (m == NULL) {
			if (last_ba != ba)
				vm_object_drop(lobject);
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
		if (last_ba != ba)
			vm_object_drop(lobject);

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
			vm_page_zero_fill(m);
			mycpu->gd_cnt.v_zfod++;
			m->valid = VM_PAGE_BITS_ALL;

			/*
			 * Handle dirty page case
			 */
			if (pprot & VM_PROT_WRITE)
				vm_set_nosync(m, entry);
			pmap_enter(pmap, addr, m, pprot, 0, entry);
#if 0
			/* REMOVE ME, a burst counts as one fault */
			mycpu->gd_cnt.v_vm_faults++;
			if (curthread->td_lwp)
				++curthread->td_lwp->lwp_ru.ru_minflt;
#endif
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
#if 0
			/* REMOVE ME, a burst counts as one fault */
			mycpu->gd_cnt.v_vm_faults++;
			if (curthread->td_lwp)
				++curthread->td_lwp->lwp_ru.ru_minflt;
#endif
			vm_page_wakeup(m);
		} else {
			vm_page_wakeup(m);
		}
	}
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
	if (entry->maptype != VM_MAPTYPE_NORMAL)
		return;
	lp = curthread->td_lwp;
	if (lp == NULL || (pmap != vmspace_pmap(lp->lwp_vmspace)))
		return;
	object = entry->ba.object;
	if (entry->ba.backing_ba != NULL)
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
		if (addr < entry->ba.start) {
			noneg = 1;
			if (noneg && nopos)
				break;
			continue;
		}
		if (addr >= entry->ba.end) {
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
		 * The page must already exist.  If we encounter a problem
		 * we stop here.
		 *
		 * WARNING!  We cannot call swap_pager_unswapped() or insert
		 *	     a new vm_page with a shared token.
		 */
		pindex = ((addr - entry->ba.start) + entry->ba.offset) >>
			 PAGE_SHIFT;

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
		 * Shortcut the read-only mapping case using the far more
		 * efficient vm_page_lookup_sbusy_try() function.  This
		 * allows us to acquire the page soft-busied only which
		 * is especially nice for concurrent execs of the same
		 * program.
		 *
		 * The lookup function also validates page suitability
		 * (all valid bits set, and not fictitious).
		 *
		 * If the page is in PQ_CACHE we have to fall-through
		 * and hard-busy it so we can move it out of PQ_CACHE.
		 */
		if ((prot & VM_PROT_WRITE) == 0) {
			m = vm_page_lookup_sbusy_try(object, pindex,
						     0, PAGE_SIZE);
			if (m == NULL)
				break;
			if ((m->queue - m->pc) != PQ_CACHE) {
				pmap_enter(pmap, addr, m, prot, 0, entry);
#if 0
			/* REMOVE ME, a burst counts as one fault */
				mycpu->gd_cnt.v_vm_faults++;
				if (curthread->td_lwp)
					++curthread->td_lwp->lwp_ru.ru_minflt;
#endif
				vm_page_sbusy_drop(m);
				continue;
			}
			vm_page_sbusy_drop(m);
		}

		/*
		 * Fallback to normal vm_page lookup code.  This code
		 * hard-busies the page.  Not only that, but the page
		 * can remain in that state for a significant period
		 * time due to pmap_enter()'s overhead.
		 */
		m = vm_page_lookup_busy_try(object, pindex, TRUE, &error);
		if (m == NULL || error)
			break;

		/*
		 * Stop if the page cannot be trivially entered into the
		 * pmap.
		 */
		if (((m->valid & VM_PAGE_BITS_ALL) != VM_PAGE_BITS_ALL) ||
		    (m->flags & PG_FICTITIOUS) ||
		    ((m->flags & PG_SWAPPED) &&
		     (prot & VM_PROT_WRITE) &&
		     (fault_flags & VM_FAULT_DIRTY))) {
			vm_page_wakeup(m);
			break;
		}

		/*
		 * Enter the page into the pmap.  The object might be held
		 * shared so we can't do any (serious) modifying operation
		 * on it.
		 */
		if ((m->queue - m->pc) == PQ_CACHE)
			vm_page_deactivate(m);
		if (prot & VM_PROT_WRITE) {
			vm_object_set_writeable_dirty(m->object);
			vm_set_nosync(m, entry);
			if (fault_flags & VM_FAULT_DIRTY) {
				vm_page_dirty(m);
				/* can't happeen due to conditional above */
				/* swap_pager_unswapped(m); */
			}
		}
		pmap_enter(pmap, addr, m, prot, 0, entry);
#if 0
		/* REMOVE ME, a burst counts as one fault */
		mycpu->gd_cnt.v_vm_faults++;
		if (curthread->td_lwp)
			++curthread->td_lwp->lwp_ru.ru_minflt;
#endif
		vm_page_wakeup(m);
	}
}
