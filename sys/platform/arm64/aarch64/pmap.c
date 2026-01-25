/*-
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * ARM64 pmap - stub implementation for compile-only MVP.
 * Real implementation will follow FreeBSD's arm64 pmap.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/msgbuf.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_pager.h>
#include <machine/pmap.h>
#include <machine/md_var.h>

/*
 * Kernel pmap and VM globals
 */
static struct pmap kernel_pmap_store;
struct pmap *kernel_pmap = &kernel_pmap_store;

vm_paddr_t phys_avail[16];
vm_paddr_t dump_avail[16];
int physmem;

vm_offset_t virtual_start;
vm_offset_t virtual_end;
vm_offset_t virtual2_start;
vm_offset_t virtual2_end;
vm_offset_t kernel_vm_end;

vm_offset_t KvaStart;
vm_offset_t KvaEnd;
vm_offset_t KvaSize;

/*
 * Initialize the pmap module.
 */
void
pmap_init(void)
{
}

/*
 * Secondary pmap initialization.
 */
void
pmap_init2(void)
{
}

/*
 * Initialize a pmap for a new process.
 */
void
pmap_pinit0(struct pmap *pmap)
{
	pmap->pm_stats.resident_count = 0;
	pmap->pm_stats.wired_count = 0;
}

/*
 * Initialize a pmap structure.
 */
void
pmap_pinit(struct pmap *pmap)
{
	pmap->pm_stats.resident_count = 0;
	pmap->pm_stats.wired_count = 0;
}

/*
 * Secondary pmap initialization after first process is created.
 */
void
pmap_pinit2(struct pmap *pmap)
{
}

/*
 * Release any resources held by the pmap structure.
 */
void
pmap_puninit(struct pmap *pmap __unused)
{
}

/*
 * Release reference to the pmap.
 */
void
pmap_release(struct pmap *pmap __unused)
{
}

/*
 * Reference the pmap.
 */
void
pmap_reference(struct pmap *pmap __unused)
{
}

/*
 * Create a pv_entry for the mapping.
 */
void
pmap_page_init(struct vm_page *m __unused)
{
}

/*
 * Initialize the thread pmap.
 */
void
pmap_init_thread(struct thread *td __unused)
{
}

/*
 * Initialize the proc pmap.
 */
void
pmap_init_proc(struct proc *p __unused)
{
}

/*
 * Enter a mapping.
 */
void
pmap_enter(pmap_t pmap __unused, vm_offset_t va __unused,
    struct vm_page *m __unused, vm_prot_t prot __unused,
    boolean_t wired __unused, struct vm_map_entry *entry __unused)
{
}

/*
 * Remove a mapping.
 */
void
pmap_remove(pmap_t pmap __unused, vm_offset_t sva __unused,
    vm_offset_t eva __unused)
{
}

/*
 * Protect mappings.
 */
void
pmap_protect(pmap_t pmap __unused, vm_offset_t sva __unused,
    vm_offset_t eva __unused, vm_prot_t prot __unused)
{
}

/*
 * Unwire a page.
 */
void
pmap_unwire(pmap_t pmap __unused, vm_offset_t va __unused)
{
}

/*
 * Copy mappings from one pmap to another.
 */
void
pmap_copy(pmap_t dst_pmap __unused, pmap_t src_pmap __unused,
    vm_offset_t dst_addr __unused, vm_size_t len __unused,
    vm_offset_t src_addr __unused)
{
}

/*
 * Extract the physical address for a virtual address.
 */
vm_paddr_t
pmap_extract(pmap_t pmap __unused, vm_offset_t va __unused, void **handlep)
{
	if (handlep)
		*handlep = NULL;
	return (0);
}

/*
 * Complete extraction.
 */
void
pmap_extract_done(void *handle __unused)
{
}

/*
 * Extract kernel virtual address to physical.
 */
vm_paddr_t
pmap_kextract(vm_offset_t va __unused)
{
	return (0);
}

/*
 * Enter a kernel mapping.
 */
void
pmap_kenter(vm_offset_t va __unused, vm_paddr_t pa __unused)
{
}

/*
 * Enter a kernel mapping without invalidation.
 */
void
pmap_kenter_noinval(vm_offset_t va __unused, vm_paddr_t pa __unused)
{
}

/*
 * Enter a kernel mapping quickly.
 */
void
pmap_kenter_quick(vm_offset_t va __unused, vm_paddr_t pa __unused)
{
}

/*
 * Remove a kernel mapping.
 */
void
pmap_kremove(vm_offset_t va __unused)
{
}

/*
 * Remove a kernel mapping quickly.
 */
void
pmap_kremove_quick(vm_offset_t va __unused)
{
}

/*
 * Enter multiple pages at once.
 */
void
pmap_qenter(vm_offset_t va __unused, struct vm_page **m __unused,
    int count __unused)
{
}

/*
 * Enter multiple pages without invalidation.
 */
void
pmap_qenter_noinval(vm_offset_t va __unused, struct vm_page **m __unused,
    int count __unused)
{
}

/*
 * Remove multiple pages.
 */
void
pmap_qremove(vm_offset_t va __unused, int count __unused)
{
}

/*
 * Remove multiple pages without invalidation.
 */
void
pmap_qremove_noinval(vm_offset_t va __unused, int count __unused)
{
}

/*
 * Zero a page.
 */
void
pmap_zero_page(vm_paddr_t pa __unused)
{
}

/*
 * Zero a page area.
 */
void
pmap_zero_page_area(vm_paddr_t pa __unused, int off __unused, int size __unused)
{
}

/*
 * Copy a page.
 */
void
pmap_copy_page(vm_paddr_t src __unused, vm_paddr_t dst __unused)
{
}

/*
 * Page is modified?
 */
int
pmap_is_modified(struct vm_page *m __unused)
{
	return (0);
}

/*
 * Clear modify bit.
 */
void
pmap_clear_modify(struct vm_page *m __unused)
{
}

/*
 * Clear reference bit.
 */
void
pmap_clear_reference(struct vm_page *m __unused)
{
}

/*
 * Test reference.
 */
int
pmap_ts_referenced(struct vm_page *m __unused)
{
	return (0);
}

/*
 * Remove all mappings from a page.
 */
void
pmap_page_protect(struct vm_page *m __unused, vm_prot_t prot __unused)
{
}

/*
 * Synchronize mapped state.
 */
void
pmap_mapped_sync(struct vm_page *m __unused)
{
}

/*
 * Collect garbage.
 */
void
pmap_collect(void)
{
}

/*
 * Remove all pages from a pmap.
 */
void
pmap_remove_pages(pmap_t pmap __unused, vm_offset_t sva __unused,
    vm_offset_t eva __unused)
{
}

/*
 * Remove specific mapping.
 */
void
pmap_remove_specific(pmap_t pmap __unused, struct vm_page *m __unused)
{
}

/*
 * Grow kernel VA.
 */
vm_offset_t
pmap_growkernel(vm_offset_t addr)
{
	return (addr);
}

/*
 * Map physical memory.
 */
void *
pmap_map(vm_offset_t *virt __unused, vm_paddr_t start __unused,
    vm_paddr_t end __unused, int prot __unused)
{
	return (NULL);
}

/*
 * Object initialization.
 */
void
pmap_object_init(struct vm_object *object __unused)
{
}

/*
 * Object free.
 */
void
pmap_object_free(struct vm_object *object __unused)
{
}

/*
 * Initialize page table for object.
 */
void
pmap_object_init_pt(pmap_t pmap __unused, vm_offset_t addr __unused,
    vm_prot_t prot __unused, struct vm_object *object __unused,
    vm_pindex_t pindex __unused, vm_size_t size __unused,
    int limit __unused)
{
}

/*
 * Prefault check.
 */
int
pmap_prefault_ok(pmap_t pmap __unused, vm_offset_t addr __unused)
{
	return (0);
}

/*
 * Mincore - determine if page is in memory.
 */
int
pmap_mincore(pmap_t pmap __unused, vm_offset_t addr __unused)
{
	return (0);
}

/*
 * Maybe threaded check.
 */
int
pmap_maybethreaded(pmap_t pmap __unused)
{
	return (0);
}

/*
 * Invalidate range.
 */
void
pmap_invalidate_range(pmap_t pmap __unused, vm_offset_t sva __unused,
    vm_offset_t eva __unused)
{
}

/*
 * Get physical address.
 */
vm_offset_t
pmap_phys_address(vm_pindex_t ppn __unused)
{
	return (0);
}

/*
 * Provide address hint.
 */
vm_offset_t
pmap_addr_hint(struct vm_object *obj __unused, vm_offset_t addr,
    vm_size_t size __unused)
{
	return (addr);
}

/*
 * Replace VM in LWP.
 */
void
pmap_replacevm(struct proc *p __unused, struct vmspace *newvm __unused,
    int flags __unused)
{
}

/*
 * Set LWP vmspace.
 */
void
pmap_setlwpvm(struct lwp *lp __unused, struct vmspace *newvm __unused)
{
}

/*
 * Set page memory attribute.
 */
void
pmap_page_set_memattr(struct vm_page *m __unused, vm_memattr_t ma __unused)
{
}

/*
 * Kernel virtual to machine page.
 */
vm_page_t
pmap_kvtom(vm_offset_t va __unused)
{
	return (NULL);
}

/*
 * Scan pages.
 */
void
pmap_pgscan(struct pmap_pgscan_info *info __unused)
{
}

/*
 * Fault page quick.
 */
struct vm_page *
pmap_fault_page_quick(pmap_t pmap __unused, vm_offset_t va __unused,
    vm_prot_t prot __unused, int *busyp)
{
	if (busyp)
		*busyp = 0;
	return (NULL);
}

/*
 * Check if address is in globaldata space.
 */
int
is_globaldata_space(vm_offset_t addr __unused, vm_offset_t size __unused)
{
	return (0);
}
