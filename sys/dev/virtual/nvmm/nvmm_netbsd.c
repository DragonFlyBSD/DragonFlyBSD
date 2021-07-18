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

os_vmspace_t *
os_vmspace_create(vaddr_t vmin, vaddr_t vmax)
{
	return uvmspace_alloc(vmin, vmax, false);
}

void
os_vmspace_destroy(os_vmspace_t *vm)
{
	uvmspace_free(vm);
}

int
os_vmspace_fault(os_vmspace_t *vm, vaddr_t va, vm_prot_t prot)
{
	return uvm_fault(&vm->vm_map, va, prot);
}

os_vmobj_t *
os_vmobj_create(voff_t size)
{
	return uao_create(size, 0);
}

void
os_vmobj_ref(os_vmobj_t *vmobj)
{
	uao_reference(vmobj);
}

void
os_vmobj_rel(os_vmobj_t *vmobj)
{
	uao_detach(vmobj);
}

int
os_vmobj_map(struct vm_map *map, vaddr_t *addr, vsize_t size, os_vmobj_t *vmobj,
    voff_t offset, bool wired, bool fixed, bool shared, int prot, int maxprot)
{
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
}

void
os_vmobj_unmap(struct vm_map *map, vaddr_t start, vaddr_t end,
    bool wired __unused)
{
	uvm_unmap(map, start, end);
}

void *
os_pagemem_zalloc(size_t size)
{
	void *ret;

	ret = (void *)uvm_km_alloc(kernel_map, roundup(size, PAGE_SIZE), 0,
	    UVM_KMF_WIRED | UVM_KMF_ZERO);

	OS_ASSERT((uintptr_t)ret % PAGE_SIZE == 0);

	return ret;
}

void
os_pagemem_free(void *ptr, size_t size)
{
	uvm_km_free(kernel_map, (vaddr_t)ptr, roundup(size, PAGE_SIZE),
	    UVM_KMF_WIRED);
}

paddr_t
os_pa_zalloc(void)
{
	struct vm_page *pg;

	pg = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO);

	return VM_PAGE_TO_PHYS(pg);
}

void
os_pa_free(paddr_t pa)
{
	uvm_pagefree(PHYS_TO_VM_PAGE(pa));
}

int
os_contigpa_zalloc(paddr_t *pa, vaddr_t *va, size_t npages)
{
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
}

void
os_contigpa_free(paddr_t pa, vaddr_t va, size_t npages)
{
	size_t i;

	pmap_kremove(va, npages * PAGE_SIZE);
	pmap_update(pmap_kernel());
	uvm_km_free(kernel_map, va, npages * PAGE_SIZE, UVM_KMF_VAONLY);
	for (i = 0; i < npages; i++) {
		uvm_pagefree(PHYS_TO_VM_PAGE(pa + i * PAGE_SIZE));
	}
}
