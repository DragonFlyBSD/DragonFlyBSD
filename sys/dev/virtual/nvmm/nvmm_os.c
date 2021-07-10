/*
 * Copyright (c) 2021 Maxime Villard, m00nbsd.net
 * All rights reserved.
 *
 * This code is part of the NVMM hypervisor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mman.h>

#include "nvmm_os.h"

#if defined(__DragonFly__)
MALLOC_DEFINE(M_NVMM, "nvmm", "NVMM data");
#endif

#if defined(__DragonFly__)
/*
 * NVMM expects VM functions to return 0 on success, but DragonFly's VM
 * functions return KERN_SUCCESS.  Although it's also defined to be 0,
 * assert it to be future-proofing.
 */
CTASSERT(KERN_SUCCESS == 0);
#endif

os_vmspace_t *
os_vmspace_create(vaddr_t vmin, vaddr_t vmax)
{
#if defined(__NetBSD__)
	return uvmspace_alloc(vmin, vmax, false);
#elif defined(__DragonFly__)
	return vmspace_alloc(vmin, vmax);
#endif
}

void
os_vmspace_destroy(os_vmspace_t *vm)
{
#if defined(__NetBSD__)
	uvmspace_free(vm);
#elif defined(__DragonFly__)
	pmap_del_all_cpus(vm);
	vmspace_rel(vm);
#endif
}

int
os_vmspace_fault(os_vmspace_t *vm, vaddr_t va, vm_prot_t prot)
{
#if defined(__NetBSD__)
	return uvm_fault(&vm->vm_map, va, prot);
#elif defined(__DragonFly__)
	int fault_flags;

	if (prot & VM_PROT_WRITE)
		fault_flags = VM_FAULT_DIRTY;
	else
		fault_flags = VM_FAULT_NORMAL;

	return vm_fault(&vm->vm_map, trunc_page(va), prot, fault_flags);
#endif
}

os_vmobj_t *
os_vmobj_create(voff_t size)
{
#if defined(__NetBSD__)
	return uao_create(size, 0);
#elif defined(__DragonFly__)
	struct vm_object *object;

	object = default_pager_alloc(NULL, size, VM_PROT_DEFAULT, 0);
	vm_object_set_flag(object, OBJ_NOSPLIT);

	return object;
#endif
}

void
os_vmobj_ref(os_vmobj_t *vmobj)
{
#if defined(__NetBSD__)
	uao_reference(vmobj);
#elif defined(__DragonFly__)
	vm_object_hold(vmobj);
	vm_object_reference_locked(vmobj);
	vm_object_drop(vmobj);
#endif
}

void
os_vmobj_rel(os_vmobj_t *vmobj)
{
#if defined(__NetBSD__)
	uao_detach(vmobj);
#elif defined(__DragonFly__)
	vm_object_deallocate(vmobj);
#endif
}

int
os_vmobj_map(struct vm_map *map, vaddr_t *addr, vsize_t size, os_vmobj_t *vmobj,
    voff_t offset, bool wired, bool fixed, bool shared, int prot, int maxprot)
{
#if defined(__NetBSD__)
	uvm_flag_t uflags, uprot, umaxprot;
	int error;

	/* Convert prot. */
	uprot = 0;
	if (prot & PROT_READ)
		uprot |= UVM_PROT_R;
	if (prot & PROT_WRITE)
		uprot |= UVM_PROT_W;
	if (prot & PROT_EXEC)
		uprot |= UVM_PROT_X;

	/* Convert maxprot. */
	umaxprot = 0;
	if (maxprot & PROT_READ)
		umaxprot |= UVM_PROT_R;
	if (maxprot & PROT_WRITE)
		umaxprot |= UVM_PROT_W;
	if (maxprot & PROT_EXEC)
		umaxprot |= UVM_PROT_X;

	uflags = UVM_MAPFLAG(uprot, umaxprot,
	    shared ? UVM_INH_SHARE : UVM_INH_NONE, UVM_ADV_RANDOM,
	    fixed ? (UVM_FLAG_FIXED | UVM_FLAG_UNMAP) : 0);

	if (!fixed) {
		/* Need to provide a hint. */
		if (map == os_curproc_map) {
			*addr = curproc->p_emul->e_vm_default_addr(curproc,
			    (vaddr_t)curproc->p_vmspace->vm_daddr, size,
			    curproc->p_vmspace->vm_map.flags & VM_MAP_TOPDOWN);
		} else {
			*addr = 0;
		}
	}

	/* Get a reference to the object. */
	os_vmobj_ref(vmobj);

	/*
	 * Map the object. This consumes the reference on success only. On
	 * failure we must drop the reference manually.
	 */
	error = uvm_map(map, addr, size, vmobj, offset, 0, uflags);
	if (error) {
		/* Drop the ref. */
		os_vmobj_rel(vmobj);
		return error;
	}

	if (wired) {
		error = uvm_map_pageable(map, *addr, *addr + size, false, 0);
		if (error) {
			os_vmobj_unmap(map, *addr, *addr + size, false);
			return error;
		}
	}

	return 0;

#elif defined(__DragonFly__)
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
#endif
}

void
os_vmobj_unmap(struct vm_map *map, vaddr_t start, vaddr_t end, bool wired)
{
#if defined(__NetBSD__)
	uvm_unmap(map, start, end);
#elif defined(__DragonFly__)
	if (wired) {
		/* Unwire kernel mappings before removing. */
		vm_map_wire(map, start, end, KM_PAGEABLE);
	}
	vm_map_remove(map, start, end);
#endif
}

void *
os_pagemem_zalloc(size_t size)
{
	void *ret;

#if defined(__NetBSD__)
	ret = (void *)uvm_km_alloc(kernel_map, roundup(size, PAGE_SIZE), 0,
	    UVM_KMF_WIRED | UVM_KMF_ZERO);
#elif defined(__DragonFly__)
	/* NOTE: kmem_alloc() may return 0 ! */
	ret = (void *)kmem_alloc(kernel_map, roundup(size, PAGE_SIZE),
	    VM_SUBSYS_NVMM);
#endif

	OS_ASSERT((uintptr_t)ret % PAGE_SIZE == 0);

	return ret;
}

void
os_pagemem_free(void *ptr, size_t size)
{
#if defined(__NetBSD__)
	uvm_km_free(kernel_map, (vaddr_t)ptr, roundup(size, PAGE_SIZE),
	    UVM_KMF_WIRED);
#elif defined(__DragonFly__)
	kmem_free(kernel_map, (vaddr_t)ptr, roundup(size, PAGE_SIZE));
#endif
}

paddr_t
os_pa_zalloc(void)
{
	struct vm_page *pg;

#if defined(__NetBSD__)
	pg = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO);
#elif defined(__DragonFly__)
	pg = vm_page_alloczwq(0,
	    VM_ALLOC_SYSTEM | VM_ALLOC_ZERO | VM_ALLOC_RETRY);
#endif

	return VM_PAGE_TO_PHYS(pg);
}

void
os_pa_free(paddr_t pa)
{
#if defined(__NetBSD__)
	uvm_pagefree(PHYS_TO_VM_PAGE(pa));
#elif defined(__DragonFly__)
	vm_page_freezwq(PHYS_TO_VM_PAGE(pa));
#endif
}

int
os_contigpa_zalloc(paddr_t *pa, vaddr_t *va, size_t npages)
{
#if defined(__NetBSD__)
	struct pglist pglist;
	paddr_t _pa;
	vaddr_t _va;
	size_t i;
	int ret;

	ret = uvm_pglistalloc(npages * PAGE_SIZE, 0, ~0UL, PAGE_SIZE, 0,
	    &pglist, 1, 0);
	if (ret != 0)
		return ENOMEM;
	_pa = VM_PAGE_TO_PHYS(TAILQ_FIRST(&pglist));
	_va = uvm_km_alloc(kernel_map, npages * PAGE_SIZE, 0,
	    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
	if (_va == 0)
		goto error;

	for (i = 0; i < npages; i++) {
		pmap_kenter_pa(_va + i * PAGE_SIZE, _pa + i * PAGE_SIZE,
		    VM_PROT_READ | VM_PROT_WRITE, PMAP_WRITE_BACK);
	}
	pmap_update(pmap_kernel());

	memset((void *)_va, 0, npages * PAGE_SIZE);

	*pa = _pa;
	*va = _va;
	return 0;

error:
	for (i = 0; i < npages; i++) {
		uvm_pagefree(PHYS_TO_VM_PAGE(_pa + i * PAGE_SIZE));
	}
	return ENOMEM;

#elif defined(__DragonFly__)
	void *addr;

	addr = contigmalloc(npages * PAGE_SIZE, M_NVMM, M_WAITOK | M_ZERO,
	    0, ~0UL, PAGE_SIZE, 0);
	if (addr == NULL)
		return ENOMEM;

	*va = (vaddr_t)addr;
	*pa = vtophys(addr);
	return 0;
#endif /* __NetBSD__ */
}

void
os_contigpa_free(paddr_t pa, vaddr_t va, size_t npages)
{
#if defined(__NetBSD__)
	size_t i;

	pmap_kremove(va, npages * PAGE_SIZE);
	pmap_update(pmap_kernel());
	uvm_km_free(kernel_map, va, npages * PAGE_SIZE, UVM_KMF_VAONLY);
	for (i = 0; i < npages; i++) {
		uvm_pagefree(PHYS_TO_VM_PAGE(pa + i * PAGE_SIZE));
	}
#elif defined(__DragonFly__)
	contigfree((void *)va, npages * PAGE_SIZE, M_NVMM);
#endif
}
