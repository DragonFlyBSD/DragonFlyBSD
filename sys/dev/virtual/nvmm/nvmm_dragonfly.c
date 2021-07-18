/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2021 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Aaron LI <aly@aaronly.me>
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mman.h>

#include "nvmm_os.h"

MALLOC_DEFINE(M_NVMM, "nvmm", "NVMM data");

/*
 * NVMM expects VM functions to return 0 on success, but DragonFly's VM
 * functions return KERN_SUCCESS.  Although it's also defined to be 0,
 * assert it to be future-proofing.
 */
CTASSERT(KERN_SUCCESS == 0);

os_vmspace_t *
os_vmspace_create(vaddr_t vmin, vaddr_t vmax)
{
	return vmspace_alloc(vmin, vmax);
}

void
os_vmspace_destroy(os_vmspace_t *vm)
{
	pmap_del_all_cpus(vm);
	vmspace_rel(vm);
}

int
os_vmspace_fault(os_vmspace_t *vm, vaddr_t va, vm_prot_t prot)
{
	int fault_flags;

	if (prot & VM_PROT_WRITE)
		fault_flags = VM_FAULT_DIRTY;
	else
		fault_flags = VM_FAULT_NORMAL;

	return vm_fault(&vm->vm_map, trunc_page(va), prot, fault_flags);
}

os_vmobj_t *
os_vmobj_create(voff_t size)
{
	struct vm_object *object;

	object = default_pager_alloc(NULL, size, VM_PROT_DEFAULT, 0);
	vm_object_set_flag(object, OBJ_NOSPLIT);

	return object;
}

void
os_vmobj_ref(os_vmobj_t *vmobj)
{
	vm_object_hold(vmobj);
	vm_object_reference_locked(vmobj);
	vm_object_drop(vmobj);
}

void
os_vmobj_rel(os_vmobj_t *vmobj)
{
	vm_object_deallocate(vmobj);
}

int
os_vmobj_map(struct vm_map *map, vaddr_t *addr, vsize_t size, os_vmobj_t *vmobj,
    voff_t offset, bool wired, bool fixed, bool shared, int prot, int maxprot)
{
	vm_prot_t vmprot, vmmaxprot;
	vm_inherit_t inherit;
	vm_offset_t start = *addr;
	int rv = KERN_SUCCESS;
	int count;

	/* Convert prot. */
	vmprot = 0;
	if (prot & PROT_READ)
		vmprot |= VM_PROT_READ;
	if (prot & PROT_WRITE)
		vmprot |= VM_PROT_WRITE;
	if (prot & PROT_EXEC)
		vmprot |= VM_PROT_EXECUTE;

	/* Convert maxprot. */
	vmmaxprot = 0;
	if (maxprot & PROT_READ)
		vmmaxprot |= VM_PROT_READ;
	if (maxprot & PROT_WRITE)
		vmmaxprot |= VM_PROT_WRITE;
	if (maxprot & PROT_EXEC)
		vmmaxprot |= VM_PROT_EXECUTE;

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);

	if (fixed) {
		/*
		 * Remove any existing entries in the range, so the new
		 * mapping can be created at the requested address.
		 */
		rv = vm_map_delete(map, start, start + size, &count);
	} else {
		if (vm_map_findspace(map, start, size, 1, 0, &start))
			rv = KERN_NO_SPACE;
	}
	if (rv != KERN_SUCCESS) {
		vm_map_unlock(map);
		vm_map_entry_release(count);
		return rv;
	}

	/* Get a reference to the object. */
	os_vmobj_ref(vmobj);

	/*
	 * Map the object. This consumes the reference on success only. On
	 * failure we must drop the reference manually.
	 */
	vm_object_hold(vmobj);
	rv = vm_map_insert(map, &count, vmobj, NULL, offset, NULL,
	    start, start + size, VM_MAPTYPE_NORMAL, VM_SUBSYS_NVMM,
	    vmprot, vmmaxprot, 0);
	vm_object_drop(vmobj);
	vm_map_unlock(map);
	vm_map_entry_release(count);
	if (rv != KERN_SUCCESS) {
		/* Drop the ref. */
		os_vmobj_rel(vmobj);
		return rv;
	}

	inherit = shared ? VM_INHERIT_SHARE : VM_INHERIT_NONE;
	rv = vm_map_inherit(map, start, start + size, inherit);
	if (rv != KERN_SUCCESS) {
		os_vmobj_unmap(map, start, start + size, false);
		return rv;
	}

	if (wired) {
		rv = vm_map_wire(map, start, start + size, 0);
		if (rv != KERN_SUCCESS) {
			os_vmobj_unmap(map, start, start + size, false);
			return rv;
		}
	}

	*addr = start;
	return 0;
}

void
os_vmobj_unmap(struct vm_map *map, vaddr_t start, vaddr_t end, bool wired)
{
	if (wired) {
		/* Unwire kernel mappings before removing. */
		vm_map_wire(map, start, end, KM_PAGEABLE);
	}
	vm_map_remove(map, start, end);
}

void *
os_pagemem_zalloc(size_t size)
{
	void *ret;

	/* NOTE: kmem_alloc() may return 0 ! */
	ret = (void *)kmem_alloc(kernel_map, roundup(size, PAGE_SIZE),
	    VM_SUBSYS_NVMM);

	OS_ASSERT((uintptr_t)ret % PAGE_SIZE == 0);

	return ret;
}

void
os_pagemem_free(void *ptr, size_t size)
{
	kmem_free(kernel_map, (vaddr_t)ptr, roundup(size, PAGE_SIZE));
}

paddr_t
os_pa_zalloc(void)
{
	struct vm_page *pg;

	pg = vm_page_alloczwq(0,
	    VM_ALLOC_SYSTEM | VM_ALLOC_ZERO | VM_ALLOC_RETRY);

	return VM_PAGE_TO_PHYS(pg);
}

void
os_pa_free(paddr_t pa)
{
	vm_page_freezwq(PHYS_TO_VM_PAGE(pa));
}

int
os_contigpa_zalloc(paddr_t *pa, vaddr_t *va, size_t npages)
{
	void *addr;

	addr = contigmalloc(npages * PAGE_SIZE, M_NVMM, M_WAITOK | M_ZERO,
	    0, ~0UL, PAGE_SIZE, 0);
	if (addr == NULL)
		return ENOMEM;

	*va = (vaddr_t)addr;
	*pa = vtophys(addr);
	return 0;
}

void
os_contigpa_free(paddr_t pa __unused, vaddr_t va, size_t npages)
{
	contigfree((void *)va, npages * PAGE_SIZE, M_NVMM);
}
