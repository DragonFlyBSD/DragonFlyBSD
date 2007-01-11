/*
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
 * $DragonFly: src/sys/vm/vm_fault.c,v 1.40 2007/01/11 20:53:42 dillon Exp $
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
#include <sys/sfbuf.h>
#include <sys/lock.h>

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

#define VM_FAULT_READ_AHEAD 8
#define VM_FAULT_READ_BEHIND 7
#define VM_FAULT_READ (VM_FAULT_READ_AHEAD+VM_FAULT_READ_BEHIND+1)

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
	int didlimit;
	int hardfault;
	int fault_flags;
	int map_generation;
	boolean_t wired;
	struct vnode *vp;
};

static int vm_fault_object(struct faultstate *, vm_pindex_t, vm_prot_t);
static int vm_fault_vpagetable(struct faultstate *, vm_pindex_t *, vpte_t, int);
static int vm_fault_additional_pages (vm_page_t, int, int, vm_page_t *, int *);
static int vm_fault_ratelimit(struct vmspace *);

static __inline void
release_page(struct faultstate *fs)
{
	vm_page_wakeup(fs->m);
	vm_page_deactivate(fs->m);
	fs->m = NULL;
}

static __inline void
unlock_map(struct faultstate *fs)
{
	if (fs->lookup_still_valid) {
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
	if (fs->object != fs->first_object) {
		vm_page_free(fs->first_m);
		vm_object_pip_wakeup(fs->object);
		fs->first_m = NULL;
	}
	fs->object = fs->first_object;
	if (relock && fs->lookup_still_valid == FALSE) {
		vm_map_lock_read(fs->map);
		fs->lookup_still_valid = TRUE;
	}
}

static void
_unlock_things(struct faultstate *fs, int dealloc)
{
	vm_object_pip_wakeup(fs->first_object);
	_cleanup_successful_fault(fs, 0);
	if (dealloc) {
		vm_object_deallocate(fs->first_object);
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
 */
int
vm_fault(vm_map_t map, vm_offset_t vaddr, vm_prot_t fault_type, int fault_flags)
{
	int result;
	vm_pindex_t first_pindex;
	struct faultstate fs;

	mycpu->gd_cnt.v_vm_faults++;

	fs.didlimit = 0;
	fs.hardfault = 0;
	fs.fault_flags = fault_flags;

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
		if (result != KERN_PROTECTION_FAILURE)
			return result;
		if ((fs.fault_flags & VM_FAULT_WIRE_MASK) != VM_FAULT_USER_WIRE)
			return result;

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
		if (result != KERN_SUCCESS)
			return result;

		/*
		 * If we don't COW now, on a user wire, the user will never
		 * be able to write to the mapping.  If we don't make this
		 * restriction, the bookkeeping would be nearly impossible.
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
	 * Make a reference to this object to prevent its disposal while we
	 * are messing with it.  Once we have the reference, the map is free
	 * to be diddled.  Since objects reference their shadows (and copies),
	 * they will stay around as well.
	 *
	 * Bump the paging-in-progress count to prevent size changes (e.g.
	 * truncation operations) during I/O.  This must be done after
	 * obtaining the vnode lock in order to avoid possible deadlocks.
	 */
	vm_object_reference(fs.first_object);
	fs.vp = vnode_pager_lock(fs.first_object);
	vm_object_pip_add(fs.first_object, 1);

	fs.lookup_still_valid = TRUE;
	fs.first_m = NULL;
	fs.object = fs.first_object;	/* so unlock_and_deallocate works */

	/*
	 * If the entry is wired we cannot change the page protection.
	 */
	if (fs.wired)
		fault_type = fs.first_prot;

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
					     fault_type);
		if (result == KERN_TRY_AGAIN)
			goto RetryFault;
		if (result != KERN_SUCCESS)
			return (result);
	}

	/*
	 * Now we have the actual (object, pindex), fault in the page.  If
	 * vm_fault_object() fails it will unlock and deallocate the FS
	 * data.   If it succeeds everything remains locked and fs->object
	 * will have an additinal PIP count if it is not equal to
	 * fs->first_object
	 *
	 * vm_fault_object will set fs->prot for the pmap operation.  It is
	 * allowed to set VM_PROT_WRITE if fault_type == VM_PROT_READ if the
	 * page can be safely written.  However, it will force a read-only
	 * mapping for a read fault if the memory is managed by a virtual
	 * page table.
	 */
	result = vm_fault_object(&fs, first_pindex, fault_type);

	if (result == KERN_TRY_AGAIN)
		goto RetryFault;
	if (result != KERN_SUCCESS)
		return (result);

	/*
	 * On success vm_fault_object() does not unlock or deallocate, and fs.m
	 * will contain a busied page.
	 *
	 * Enter the page into the pmap and do pmap-related adjustments.
	 */
	unlock_things(&fs);
	pmap_enter(fs.map->pmap, vaddr, fs.m, fs.prot, fs.wired);

	if (((fs.fault_flags & VM_FAULT_WIRE_MASK) == 0) && (fs.wired == 0)) {
		pmap_prefault(fs.map->pmap, vaddr, fs.entry);
	}

	vm_page_flag_clear(fs.m, PG_ZERO);
	vm_page_flag_set(fs.m, PG_MAPPED|PG_REFERENCED);

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

	if (curthread->td_lwp) {
		if (fs.hardfault) {
			curthread->td_lwp->lwp_ru.ru_majflt++;
		} else {
			curthread->td_lwp->lwp_ru.ru_minflt++;
		}
	}

	/*
	 * Unlock everything, and return
	 */
	vm_page_wakeup(fs.m);
	vm_object_deallocate(fs.first_object);

	return (KERN_SUCCESS);
}

/*
 * Fault in the specified virtual address in the current process map, 
 * returning a held VM page or NULL.  See vm_fault_page() for more 
 * information.
 */
vm_page_t
vm_fault_page_quick(vm_offset_t va, vm_prot_t fault_type, int *errorp)
{
	vm_page_t m;

	m = vm_fault_page(&curproc->p_vmspace->vm_map, va, 
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
 */
vm_page_t
vm_fault_page(vm_map_t map, vm_offset_t vaddr, vm_prot_t fault_type,
	      int fault_flags, int *errorp)
{
	int result;
	vm_pindex_t first_pindex;
	struct faultstate fs;

	mycpu->gd_cnt.v_vm_faults++;

	fs.didlimit = 0;
	fs.hardfault = 0;
	fs.fault_flags = fault_flags;
	KKASSERT((fault_flags & VM_FAULT_WIRE_MASK) == 0);

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
		return (NULL);
	}

	/*
	 * fs.map is read-locked
	 *
	 * Misc checks.  Save the map generation number to detect races.
	 */
	fs.map_generation = fs.map->timestamp;

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
	 * Make a reference to this object to prevent its disposal while we
	 * are messing with it.  Once we have the reference, the map is free
	 * to be diddled.  Since objects reference their shadows (and copies),
	 * they will stay around as well.
	 *
	 * Bump the paging-in-progress count to prevent size changes (e.g.
	 * truncation operations) during I/O.  This must be done after
	 * obtaining the vnode lock in order to avoid possible deadlocks.
	 */
	vm_object_reference(fs.first_object);
	fs.vp = vnode_pager_lock(fs.first_object);
	vm_object_pip_add(fs.first_object, 1);

	fs.lookup_still_valid = TRUE;
	fs.first_m = NULL;
	fs.object = fs.first_object;	/* so unlock_and_deallocate works */

	/*
	 * If the entry is wired we cannot change the page protection.
	 */
	if (fs.wired)
		fault_type = fs.first_prot;

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
					     fault_type);
		if (result == KERN_TRY_AGAIN)
			goto RetryFault;
		if (result != KERN_SUCCESS) {
			*errorp = result;
			return (NULL);
		}
	}

	/*
	 * Now we have the actual (object, pindex), fault in the page.  If
	 * vm_fault_object() fails it will unlock and deallocate the FS
	 * data.   If it succeeds everything remains locked and fs->object
	 * will have an additinal PIP count if it is not equal to
	 * fs->first_object
	 */
	result = vm_fault_object(&fs, first_pindex, fault_type);

	if (result == KERN_TRY_AGAIN)
		goto RetryFault;
	if (result != KERN_SUCCESS) {
		*errorp = result;
		return(NULL);
	}

	/*
	 * On success vm_fault_object() does not unlock or deallocate, and fs.m
	 * will contain a busied page.
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
	vm_page_flag_clear(fs.m, PG_ZERO);
	if (fault_type & VM_PROT_WRITE)
		vm_page_dirty(fs.m);

	/*
	 * Update the pmap.  We really only have to do this if a COW
	 * occured to replace the read-only page with the new page.  For
	 * now just do it unconditionally. XXX
	 */
	pmap_enter(fs.map->pmap, vaddr, fs.m, fs.prot, fs.wired);
	vm_page_flag_set(fs.m, PG_REFERENCED|PG_MAPPED);

	/*
	 * Unbusy the page by activating it.  It remains held and will not
	 * be reclaimed.
	 */
	vm_page_activate(fs.m);

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
	vm_object_deallocate(fs.first_object);

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
		    vpte_t vpte, int fault_type)
{
	struct sf_buf *sf;
	int vshift = 32 - PAGE_SHIFT;	/* page index bits remaining */
	int result = KERN_SUCCESS;
	vpte_t *ptep;

	for (;;) {
		/*
		 * We cannot proceed if the vpte is not valid, not readable
		 * for a read fault, or not writable for a write fault.
		 */
		if ((vpte & VPTE_V) == 0) {
			unlock_and_deallocate(fs);
			return (KERN_FAILURE);
		}
		if ((fault_type & VM_PROT_READ) && (vpte & VPTE_R) == 0) {
			unlock_and_deallocate(fs);
			return (KERN_FAILURE);
		}
		if ((fault_type & VM_PROT_WRITE) && (vpte & VPTE_W) == 0) {
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
		result = vm_fault_object(fs, vpte >> PAGE_SHIFT,
					 VM_PROT_READ|VM_PROT_WRITE);
		if (result != KERN_SUCCESS)
			return (result);

		/*
		 * Process the returned fs.m and look up the page table
		 * entry in the page table page.
		 */
		vshift -= VPTE_PAGE_BITS;
		sf = sf_buf_alloc(fs->m, SFB_CPUPRIVATE);
		ptep = ((vpte_t *)sf_buf_kva(sf) +
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
		if ((fault_type & VM_PROT_WRITE) && (vpte & VPTE_V) &&
		    (vpte & VPTE_W)) {
			if ((vpte & (VPTE_M|VPTE_A)) != (VPTE_M|VPTE_A)) {
				atomic_set_int(ptep, VPTE_M|VPTE_A);
				vm_page_dirty(fs->m);
			}
		}
		if ((fault_type & VM_PROT_READ) && (vpte & VPTE_V) &&
		    (vpte & VPTE_R)) {
			if ((vpte & VPTE_A) == 0) {
				atomic_set_int(ptep, VPTE_A);
				vm_page_dirty(fs->m);
			}
		}
		sf_buf_free(sf);
		vm_page_flag_set(fs->m, PG_REFERENCED);
		vm_page_activate(fs->m);
		vm_page_wakeup(fs->m);
		cleanup_successful_fault(fs);
	}
	/*
	 * Combine remaining address bits with the vpte.
	 */
	*pindex = (vpte >> PAGE_SHIFT) +
		  (*pindex & ((1 << vshift) - 1));
	return (KERN_SUCCESS);
}


/*
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
 */
static
int
vm_fault_object(struct faultstate *fs,
		vm_pindex_t first_pindex, vm_prot_t fault_type)
{
	vm_object_t next_object;
	vm_page_t marray[VM_FAULT_READ];
	vm_pindex_t pindex;
	int faultcount;

	fs->prot = fs->first_prot;
	fs->object = fs->first_object;
	pindex = first_pindex;

	/* 
	 * If a read fault occurs we try to make the page writable if
	 * possible.  There are three cases where we cannot make the
	 * page mapping writable:
	 *
	 * (1) The mapping is read-only or the VM object is read-only,
	 *     fs->prot above will simply not have VM_PROT_WRITE set.
	 *
	 * (2) If the mapping is a virtual page table we need to be able
	 *     to detect writes so we can set VPTE_M.
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

	for (;;) {
		/*
		 * If the object is dead, we stop here
		 */
		if (fs->object->flags & OBJ_DEAD) {
			unlock_and_deallocate(fs);
			return (KERN_PROTECTION_FAILURE);
		}

		/*
		 * See if page is resident.  spl protection is required
		 * to avoid an interrupt unbusy/free race against our
		 * lookup.  We must hold the protection through a page
		 * allocation or busy.
		 */
		crit_enter();
		fs->m = vm_page_lookup(fs->object, pindex);
		if (fs->m != NULL) {
			int queue;
			/*
			 * Wait/Retry if the page is busy.  We have to do this
			 * if the page is busy via either PG_BUSY or 
			 * vm_page_t->busy because the vm_pager may be using
			 * vm_page_t->busy for pageouts ( and even pageins if
			 * it is the vnode pager ), and we could end up trying
			 * to pagein and pageout the same page simultaneously.
			 *
			 * We can theoretically allow the busy case on a read
			 * fault if the page is marked valid, but since such
			 * pages are typically already pmap'd, putting that
			 * special case in might be more effort then it is 
			 * worth.  We cannot under any circumstances mess
			 * around with a vm_page_t->busy page except, perhaps,
			 * to pmap it.
			 */
			if ((fs->m->flags & PG_BUSY) || fs->m->busy) {
				unlock_things(fs);
				vm_page_sleep_busy(fs->m, TRUE, "vmpfw");
				mycpu->gd_cnt.v_intrans++;
				vm_object_deallocate(fs->first_object);
				crit_exit();
				return (KERN_TRY_AGAIN);
			}

			/*
			 * If reactivating a page from PQ_CACHE we may have
			 * to rate-limit.
			 */
			queue = fs->m->queue;
			vm_page_unqueue_nowakeup(fs->m);

			if ((queue - fs->m->pc) == PQ_CACHE && 
			    vm_page_count_severe()) {
				vm_page_activate(fs->m);
				unlock_and_deallocate(fs);
				vm_waitpfault();
				crit_exit();
				return (KERN_TRY_AGAIN);
			}

			/*
			 * Mark page busy for other processes, and the 
			 * pagedaemon.  If it still isn't completely valid
			 * (readable), jump to readrest, else we found the
			 * page and can return.
			 *
			 * We can release the spl once we have marked the
			 * page busy.
			 */
			vm_page_busy(fs->m);
			crit_exit();

			if (((fs->m->valid & VM_PAGE_BITS_ALL) != VM_PAGE_BITS_ALL) &&
			    fs->m->object != &kernel_object) {
				goto readrest;
			}
			break; /* break to PAGE HAS BEEN FOUND */
		}

		/*
		 * Page is not resident, If this is the search termination
		 * or the pager might contain the page, allocate a new page.
		 *
		 * NOTE: We are still in a critical section.
		 */
		if (TRYPAGER(fs) || fs->object == fs->first_object) {
			/*
			 * If the page is beyond the object size we fail
			 */
			if (pindex >= fs->object->size) {
				crit_exit();
				unlock_and_deallocate(fs);
				return (KERN_PROTECTION_FAILURE);
			}

			/*
			 * Ratelimit.
			 */
			if (fs->didlimit == 0 && curproc != NULL) {
				int limticks;

				limticks = vm_fault_ratelimit(curproc->p_vmspace);
				if (limticks) {
					crit_exit();
					unlock_and_deallocate(fs);
					tsleep(curproc, 0, "vmrate", limticks);
					fs->didlimit = 1;
					return (KERN_TRY_AGAIN);
				}
			}

			/*
			 * Allocate a new page for this object/offset pair.
			 */
			fs->m = NULL;
			if (!vm_page_count_severe()) {
				fs->m = vm_page_alloc(fs->object, pindex,
				    (fs->vp || fs->object->backing_object) ? VM_ALLOC_NORMAL : VM_ALLOC_NORMAL | VM_ALLOC_ZERO);
			}
			if (fs->m == NULL) {
				crit_exit();
				unlock_and_deallocate(fs);
				vm_waitpfault();
				return (KERN_TRY_AGAIN);
			}
		}
		crit_exit();

readrest:
		/*
		 * We have found a valid page or we have allocated a new page.
		 * The page thus may not be valid or may not be entirely 
		 * valid.
		 *
		 * Attempt to fault-in the page if there is a chance that the
		 * pager has it, and potentially fault in additional pages
		 * at the same time.
		 *
		 * We are NOT in splvm here and if TRYPAGER is true then
		 * fs.m will be non-NULL and will be PG_BUSY for us.
		 */

		if (TRYPAGER(fs)) {
			int rv;
			int reqpage;
			int ahead, behind;
			u_char behavior = vm_map_entry_behavior(fs->entry);

			if (behavior == MAP_ENTRY_BEHAV_RANDOM) {
				ahead = 0;
				behind = 0;
			} else {
				behind = pindex;
				if (behind > VM_FAULT_READ_BEHIND)
					behind = VM_FAULT_READ_BEHIND;

				ahead = fs->object->size - pindex;
				if (ahead < 1)
					ahead = 1;
				if (ahead > VM_FAULT_READ_AHEAD)
					ahead = VM_FAULT_READ_AHEAD;
			}

			if ((fs->first_object->type != OBJT_DEVICE) &&
			    (behavior == MAP_ENTRY_BEHAV_SEQUENTIAL ||
                                (behavior != MAP_ENTRY_BEHAV_RANDOM &&
                                pindex >= fs->entry->lastr &&
                                pindex < fs->entry->lastr + VM_FAULT_READ))
			) {
				vm_pindex_t firstpindex, tmppindex;

				if (first_pindex < 2 * VM_FAULT_READ)
					firstpindex = 0;
				else
					firstpindex = first_pindex - 2 * VM_FAULT_READ;

				/*
				 * note: partially valid pages cannot be 
				 * included in the lookahead - NFS piecemeal
				 * writes will barf on it badly.
				 *
				 * spl protection is required to avoid races
				 * between the lookup and an interrupt
				 * unbusy/free sequence occuring prior to
				 * our busy check.
				 */
				crit_enter();
				for (tmppindex = first_pindex - 1;
				    tmppindex >= firstpindex;
				    --tmppindex
				) {
					vm_page_t mt;

					mt = vm_page_lookup(fs->first_object, tmppindex);
					if (mt == NULL || (mt->valid != VM_PAGE_BITS_ALL))
						break;
					if (mt->busy ||
						(mt->flags & (PG_BUSY | PG_FICTITIOUS | PG_UNMANAGED)) ||
						mt->hold_count ||
						mt->wire_count) 
						continue;
					if (mt->dirty == 0)
						vm_page_test_dirty(mt);
					if (mt->dirty) {
						vm_page_protect(mt, VM_PROT_NONE);
						vm_page_deactivate(mt);
					} else {
						vm_page_cache(mt);
					}
				}
				crit_exit();

				ahead += behind;
				behind = 0;
			}

			/*
			 * now we find out if any other pages should be paged
			 * in at this time this routine checks to see if the
			 * pages surrounding this fault reside in the same
			 * object as the page for this fault.  If they do,
			 * then they are faulted in also into the object.  The
			 * array "marray" returned contains an array of
			 * vm_page_t structs where one of them is the
			 * vm_page_t passed to the routine.  The reqpage
			 * return value is the index into the marray for the
			 * vm_page_t passed to the routine.
			 *
			 * fs.m plus the additional pages are PG_BUSY'd.
			 */
			faultcount = vm_fault_additional_pages(
			    fs->m, behind, ahead, marray, &reqpage);

			/*
			 * update lastr imperfectly (we do not know how much
			 * getpages will actually read), but good enough.
			 */
			fs->entry->lastr = pindex + faultcount - behind;

			/*
			 * Call the pager to retrieve the data, if any, after
			 * releasing the lock on the map.  We hold a ref on
			 * fs.object and the pages are PG_BUSY'd.
			 */
			unlock_map(fs);

			if (faultcount) {
				rv = vm_pager_get_pages(fs->object, marray, 
							faultcount, reqpage);
			} else {
				rv = VM_PAGER_FAIL;
			}

			if (rv == VM_PAGER_OK) {
				/*
				 * Found the page. Leave it busy while we play
				 * with it.
				 */

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
				if (curproc)
					kprintf("vm_fault: pager read error, pid %d (%s)\n", curproc->p_pid, curproc->p_comm);
				else
					kprintf("vm_fault: pager read error, thread %p (%s)\n", curthread, curproc->p_comm);
			}
			/*
			 * Data outside the range of the pager or an I/O error
			 */
			/*
			 * XXX - the check for kernel_map is a kludge to work
			 * around having the machine panic on a kernel space
			 * fault w/ I/O error.
			 */
			if (((fs->map != &kernel_map) && (rv == VM_PAGER_ERROR)) ||
				(rv == VM_PAGER_BAD)) {
				vm_page_free(fs->m);
				fs->m = NULL;
				unlock_and_deallocate(fs);
				if (rv == VM_PAGER_ERROR)
					return (KERN_FAILURE);
				else
					return (KERN_PROTECTION_FAILURE);
				/* NOT REACHED */
			}
			if (fs->object != fs->first_object) {
				vm_page_free(fs->m);
				fs->m = NULL;
				/*
				 * XXX - we cannot just fall out at this
				 * point, m has been freed and is invalid!
				 */
			}
		}

		/*
		 * We get here if the object has a default pager (or unwiring) 
		 * or the pager doesn't have the page.
		 */
		if (fs->object == fs->first_object)
			fs->first_m = fs->m;

		/*
		 * Move on to the next object.  Lock the next object before
		 * unlocking the current one.
		 */
		pindex += OFF_TO_IDX(fs->object->backing_object_offset);
		next_object = fs->object->backing_object;
		if (next_object == NULL) {
			/*
			 * If there's no object left, fill the page in the top
			 * object with zeros.
			 */
			if (fs->object != fs->first_object) {
				vm_object_pip_wakeup(fs->object);

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
				mycpu->gd_cnt.v_ozfod++;
			}
			mycpu->gd_cnt.v_zfod++;
			fs->m->valid = VM_PAGE_BITS_ALL;
			break;	/* break to PAGE HAS BEEN FOUND */
		} else {
			if (fs->object != fs->first_object) {
				vm_object_pip_wakeup(fs->object);
			}
			KASSERT(fs->object != next_object, ("object loop %p", next_object));
			fs->object = next_object;
			vm_object_pip_add(fs->object, 1);
		}
	}

	KASSERT((fs->m->flags & PG_BUSY) != 0,
		("vm_fault: not busy after main loop"));

	/*
	 * PAGE HAS BEEN FOUND. [Loop invariant still holds -- the object lock
	 * is held.]
	 */

	/*
	 * If the page is being written, but isn't already owned by the
	 * top-level object, we have to copy it into a new page owned by the
	 * top-level object.
	 */
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
			if (fs->map_generation == fs->map->timestamp &&
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
				 lockmgr(&fs->map->lock, LK_EXCLUSIVE|LK_NOWAIT) == 0)
			    ) {
				
				fs->lookup_still_valid = 1;
				/*
				 * get rid of the unnecessary page
				 */
				vm_page_protect(fs->first_m, VM_PROT_NONE);
				vm_page_free(fs->first_m);
				fs->first_m = NULL;

				/*
				 * grab the page and put it into the 
				 * process'es object.  The page is 
				 * automatically made dirty.
				 */
				vm_page_rename(fs->m, fs->first_object, first_pindex);
				fs->first_m = fs->m;
				vm_page_busy(fs->first_m);
				fs->m = NULL;
				mycpu->gd_cnt.v_cow_optim++;
			} else {
				/*
				 * Oh, well, lets copy it.
				 */
				vm_page_copy(fs->m, fs->first_m);
			}

			if (fs->m) {
				/*
				 * We no longer need the old page or object.
				 */
				release_page(fs);
			}

			/*
			 * fs->object != fs->first_object due to above 
			 * conditional
			 */
			vm_object_pip_wakeup(fs->object);

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
	 * We may have had to unlock a map to do I/O.  If we did then
	 * lookup_still_valid will be FALSE.  If the map generation count
	 * also changed then all sorts of things could have happened while
	 * we were doing the I/O and we need to retry.
	 */

	if (!fs->lookup_still_valid &&
	    (fs->map->timestamp != fs->map_generation)) {
		release_page(fs);
		unlock_and_deallocate(fs);
		return (KERN_TRY_AGAIN);
	}

	/*
	 * Put this page into the physical map. We had to do the unlock above
	 * because pmap_enter may cause other faults.   We don't put the page
	 * back on the active queue until later so that the page-out daemon
	 * won't find us (yet).
	 */
	if (fs->prot & VM_PROT_WRITE) {
		vm_page_flag_set(fs->m, PG_WRITEABLE);
		vm_object_set_writeable_dirty(fs->m->object);

		/*
		 * If the fault is a write, we know that this page is being
		 * written NOW so dirty it explicitly to save on 
		 * pmap_is_modified() calls later.
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
		if (fs->entry->eflags & MAP_ENTRY_NOSYNC) {
			if (fs->m->dirty == 0)
				vm_page_flag_set(fs->m, PG_NOSYNC);
		} else {
			vm_page_flag_clear(fs->m, PG_NOSYNC);
		}
		if (fs->fault_flags & VM_FAULT_DIRTY) {
			crit_enter();
			vm_page_dirty(fs->m);
			vm_pager_page_unswapped(fs->m);
			crit_exit();
		}
	}

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

	return (KERN_SUCCESS);
}

/*
 * Wire down a range of virtual addresses in a map.  The entry in question
 * should be marked in-transition and the map must be locked.  We must
 * release the map temporarily while faulting-in the page to avoid a
 * deadlock.  Note that the entry may be clipped while we are blocked but
 * will never be freed.
 */
int
vm_fault_wire(vm_map_t map, vm_map_entry_t entry, boolean_t user_wire)
{
	boolean_t fictitious;
	vm_offset_t start;
	vm_offset_t end;
	vm_offset_t va;
	vm_paddr_t pa;
	pmap_t pmap;
	int rv;

	pmap = vm_map_pmap(map);
	start = entry->start;
	end = entry->end;
	fictitious = entry->object.vm_object &&
			(entry->object.vm_object->type == OBJT_DEVICE);

	vm_map_unlock(map);
	map->timestamp++;

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
				pmap_change_wiring(pmap, va, FALSE);
				if (!fictitious)
					vm_page_unwire(PHYS_TO_VM_PAGE(pa), 1);
			}
			vm_map_lock(map);
			return (rv);
		}
	}
	vm_map_lock(map);
	return (KERN_SUCCESS);
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
	pmap_t pmap;

	pmap = vm_map_pmap(map);
	start = entry->start;
	end = entry->end;
	fictitious = entry->object.vm_object &&
			(entry->object.vm_object->type == OBJT_DEVICE);

	/*
	 * Since the pages are wired down, we must be able to get their
	 * mappings from the physical map system.
	 */
	for (va = start; va < end; va += PAGE_SIZE) {
		pa = pmap_extract(pmap, va);
		if (pa != 0) {
			pmap_change_wiring(pmap, va, FALSE);
			if (!fictitious)
				vm_page_unwire(PHYS_TO_VM_PAGE(pa), 1);
		}
	}
}

/*
 * Reduce the rate at which memory is allocated to a process based
 * on the perceived load on the VM system. As the load increases
 * the allocation burst rate goes down and the delay increases. 
 *
 * Rate limiting does not apply when faulting active or inactive
 * pages.  When faulting 'cache' pages, rate limiting only applies
 * if the system currently has a severe page deficit.
 *
 * XXX vm_pagesupply should be increased when a page is freed.
 *
 * We sleep up to 1/10 of a second.
 */
static int
vm_fault_ratelimit(struct vmspace *vmspace)
{
	if (vm_load_enable == 0)
		return(0);
	if (vmspace->vm_pagesupply > 0) {
		--vmspace->vm_pagesupply;
		return(0);
	}
#ifdef INVARIANTS
	if (vm_load_debug) {
		kprintf("load %-4d give %d pgs, wait %d, pid %-5d (%s)\n",
			vm_load, 
			(1000 - vm_load ) / 10, vm_load * hz / 10000,
			curproc->p_pid, curproc->p_comm);
	}
#endif
	vmspace->vm_pagesupply = (1000 - vm_load) / 10;
	return(vm_load * hz / 10000);
}

/*
 *	Routine:
 *		vm_fault_copy_entry
 *	Function:
 *		Copy all of the pages from a wired-down map entry to another.
 *
 *	In/out conditions:
 *		The source and destination maps must be locked for write.
 *		The source map entry must be wired down (or be a sharing map
 *		entry corresponding to a main map entry that is wired down).
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

#ifdef	lint
	src_map++;
#endif	/* lint */

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
	for (vaddr = dst_entry->start, dst_offset = 0;
	    vaddr < dst_entry->end;
	    vaddr += PAGE_SIZE, dst_offset += PAGE_SIZE) {

		/*
		 * Allocate a page in the destination object
		 */
		do {
			dst_m = vm_page_alloc(dst_object,
				OFF_TO_IDX(dst_offset), VM_ALLOC_NORMAL);
			if (dst_m == NULL) {
				vm_wait();
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

		vm_page_flag_clear(dst_m, PG_ZERO);
		pmap_enter(dst_map->pmap, vaddr, dst_m, prot, FALSE);
		vm_page_flag_set(dst_m, PG_WRITEABLE|PG_MAPPED);

		/*
		 * Mark it no longer busy, and put it on the active list.
		 */
		vm_page_activate(dst_m);
		vm_page_wakeup(dst_m);
	}
}


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
	if (object->type == OBJT_DEVICE) {
		*reqpage = 0;
		marray[0] = m;
		return 1;
	}

	/*
	 * if the requested page is not available, then give up now
	 */

	if (!vm_pager_has_page(object, pindex, &cbehind, &cahead)) {
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
	 * try to do any readahead that we might have free pages for.
	 */
	if ((rahead + rbehind) >
		((vmstats.v_free_count + vmstats.v_cache_count) - vmstats.v_free_reserved)) {
		pagedaemon_wakeup();
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

		crit_enter();
		for ( tpindex = pindex - 1; tpindex >= startpindex; tpindex -= 1) {
			if (vm_page_lookup( object, tpindex)) {
				startpindex = tpindex + 1;
				break;
			}
			if (tpindex == 0)
				break;
		}

		for(i = 0, tpindex = startpindex; tpindex < pindex; i++, tpindex++) {

			rtm = vm_page_alloc(object, tpindex, VM_ALLOC_NORMAL);
			if (rtm == NULL) {
				crit_exit();
				for (j = 0; j < i; j++) {
					vm_page_free(marray[j]);
				}
				marray[0] = m;
				*reqpage = 0;
				return 1;
			}

			marray[i] = rtm;
		}
		crit_exit();
	} else {
		startpindex = 0;
		i = 0;
	}

	marray[i] = m;
	/* page offset of the required page */
	*reqpage = i;

	tpindex = pindex + 1;
	i++;

	/*
	 * scan forward for the read ahead pages
	 */
	endpindex = tpindex + rahead;
	if (endpindex > object->size)
		endpindex = object->size;

	crit_enter();
	for( ; tpindex < endpindex; i++, tpindex++) {

		if (vm_page_lookup(object, tpindex)) {
			break;
		}

		rtm = vm_page_alloc(object, tpindex, VM_ALLOC_NORMAL);
		if (rtm == NULL) {
			break;
		}

		marray[i] = rtm;
	}
	crit_exit();

	/* return number of bytes of pages */
	return i;
}
