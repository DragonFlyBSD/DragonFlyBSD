/*
 * Copyright (c) 2003-2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Mihai Carabas <mihai.carabas@gmail.com>
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

#include <sys/systm.h>
#include <sys/sfbuf.h>
#include <sys/proc.h>
#include <sys/thread.h>

#include <machine/pmap.h>
#include <machine/specialreg.h>
#include <machine/cpufunc.h>
#include <machine/vmm.h>

#include <vm/vm_extern.h>
#include <vm/vm_map.h>

#include "vmx.h"
#include "ept.h"
#include "vmm_utils.h"

static uint64_t pmap_bits_ept[PG_BITS_SIZE];
static pt_entry_t pmap_cache_bits_ept[PAT_INDEX_SIZE];
static int ept_protection_codes[PROTECTION_CODES_SIZE];
static pt_entry_t pmap_cache_mask_ept;

static int pmap_pm_flags_ept;
static int eptp_bits;

extern uint64_t vmx_ept_vpid_cap;

int
vmx_ept_init(void)
{
	int prot;
	/* Chapter 28 VMX SUPPORT FOR ADDRESS TRANSLATION
	 * Intel Manual 3c, page 107
	 */
	vmx_ept_vpid_cap = rdmsr(IA32_VMX_EPT_VPID_CAP);

	if(!EPT_PWL4(vmx_ept_vpid_cap)||
	    !EPT_MEMORY_TYPE_WB(vmx_ept_vpid_cap)) {
		return EINVAL;
	}

	eptp_bits |= EPTP_CACHE(PAT_WRITE_BACK) |
	    EPTP_PWLEN(EPT_PWLEVELS - 1);

	if (EPT_AD_BITS_SUPPORTED(vmx_ept_vpid_cap)) {
		eptp_bits |= EPTP_AD_ENABLE;
	} else {
		pmap_pm_flags_ept = PMAP_EMULATE_AD_BITS;
	}

	/* Initialize EPT bits
	 * - for PG_V - set READ and EXECUTE to preserve compatibility
	 * - for PG_U and PG_G - set 0 to preserve compatiblity
	 * - for PG_N - set the Uncacheable bit
	 */
	pmap_bits_ept[TYPE_IDX] = EPT_PMAP;
	pmap_bits_ept[PG_V_IDX] = EPT_PG_READ | EPT_PG_EXECUTE;
	pmap_bits_ept[PG_RW_IDX] = EPT_PG_WRITE;
	pmap_bits_ept[PG_PS_IDX] = EPT_PG_PS;
	pmap_bits_ept[PG_G_IDX] = 0;
	pmap_bits_ept[PG_U_IDX] = 0;
	pmap_bits_ept[PG_A_IDX] = EPT_PG_A;
	pmap_bits_ept[PG_M_IDX] = EPT_PG_M;
	pmap_bits_ept[PG_W_IDX] = EPT_PG_AVAIL1;
	pmap_bits_ept[PG_MANAGED_IDX] = EPT_PG_AVAIL2;
	pmap_bits_ept[PG_DEVICE_IDX] = EPT_PG_AVAIL3;
	pmap_bits_ept[PG_N_IDX] = EPT_IGNORE_PAT | EPT_MEM_TYPE_UC;


	pmap_cache_mask_ept = EPT_IGNORE_PAT | EPT_MEM_TYPE_MASK;

	pmap_cache_bits_ept[PAT_UNCACHEABLE] = EPT_IGNORE_PAT | EPT_MEM_TYPE_UC;
	pmap_cache_bits_ept[PAT_WRITE_COMBINING] = EPT_IGNORE_PAT | EPT_MEM_TYPE_WC;
	pmap_cache_bits_ept[PAT_WRITE_THROUGH] = EPT_IGNORE_PAT | EPT_MEM_TYPE_WT;
	pmap_cache_bits_ept[PAT_WRITE_PROTECTED] = EPT_IGNORE_PAT | EPT_MEM_TYPE_WP;
	pmap_cache_bits_ept[PAT_WRITE_BACK] = EPT_IGNORE_PAT | EPT_MEM_TYPE_WB;
	pmap_cache_bits_ept[PAT_UNCACHED] = EPT_IGNORE_PAT | EPT_MEM_TYPE_UC;

	for (prot = 0; prot < PROTECTION_CODES_SIZE; prot++) {
		switch (prot) {
		case VM_PROT_NONE | VM_PROT_NONE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_NONE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_NONE | VM_PROT_EXECUTE:
		case VM_PROT_NONE | VM_PROT_NONE | VM_PROT_EXECUTE:
			ept_protection_codes[prot] = 0;
			break;
		case VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_NONE:
		case VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_EXECUTE:
		case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE:
			ept_protection_codes[prot] = pmap_bits_ept[PG_RW_IDX];

			break;
		}
	}

	return 0;
}

/* Build the VMCS_EPTP pointer
 * - the ept_address
 * - the EPTP bits indicating optional features
 */
uint64_t vmx_eptp(uint64_t ept_address)
{
	return (ept_address | eptp_bits);
}

/* Copyin from guest VMM */
static int
ept_copyin(const void *udaddr, void *kaddr, size_t len)
{
	struct lwbuf *lwb;
	struct lwbuf lwb_cache;
	vm_page_t m;
	register_t gpa;
	size_t n;
	int err = 0;
	struct vmspace *vm = curproc->p_vmspace;
	struct vmx_thread_info *vti = curthread->td_vmm;
	register_t guest_cr3 = vti->guest_cr3;

	while (len) {
		/* Get the GPA by manually walking the-GUEST page table*/
		err = guest_phys_addr(vm, &gpa, guest_cr3, (vm_offset_t)udaddr);
		if (err) {
			kprintf("%s: could not get guest_phys_addr\n", __func__);
			break;
		}

		m = vm_fault_page(&vm->vm_map, trunc_page(gpa),
		    VM_PROT_READ, VM_FAULT_NORMAL, &err);
		if (err) {
			kprintf("%s: could not fault in vm map, gpa: %llx\n",
			    __func__, (unsigned long long) gpa);
			break;
		}

		n = PAGE_SIZE - ((vm_offset_t)udaddr & PAGE_MASK);
		if (n > len)
			n = len;

		lwb = lwbuf_alloc(m, &lwb_cache);
		bcopy((char *)lwbuf_kva(lwb)+((vm_offset_t)udaddr & PAGE_MASK), kaddr, n);
		len -= n;
		udaddr = (const char *)udaddr + n;
		kaddr = (char *)kaddr + n;
		lwbuf_free(lwb);
		vm_page_unhold(m);
	}
	if (err)
		err = EFAULT;
	return (err);
}

/* Copyout from guest VMM */
static int
ept_copyout(const void *kaddr, void *udaddr, size_t len)
{
	struct lwbuf *lwb;
	struct lwbuf lwb_cache;
	vm_page_t m;
	register_t gpa;
	size_t n;
	int err = 0;
	struct vmspace *vm = curproc->p_vmspace;
	struct vmx_thread_info *vti = curthread->td_vmm;
	register_t guest_cr3 = vti->guest_cr3;

	while (len) {
		/* Get the GPA by manually walking the-GUEST page table*/
		err = guest_phys_addr(vm, &gpa, guest_cr3, (vm_offset_t)udaddr);
		if (err) {
			kprintf("%s: could not get guest_phys_addr\n", __func__);
			break;
		}

		m = vm_fault_page(&vm->vm_map, trunc_page(gpa),
		    VM_PROT_READ | VM_PROT_WRITE,
		    VM_FAULT_NORMAL, &err);
		if (err) {
			kprintf("%s: could not fault in vm map, gpa: %llx\n",
			    __func__, (unsigned long long) gpa);
			break;
		}

		n = PAGE_SIZE - ((vm_offset_t)udaddr & PAGE_MASK);
		if (n > len)
			n = len;

		lwb = lwbuf_alloc(m, &lwb_cache);
		bcopy(kaddr, (char *)lwbuf_kva(lwb) +
			     ((vm_offset_t)udaddr & PAGE_MASK), n);

		len -= n;
		udaddr = (char *)udaddr + n;
		kaddr = (const char *)kaddr + n;
		vm_page_dirty(m);
#if 0
		/* should not be needed */
		cpu_invlpg((char *)lwbuf_kva(lwb) +
			     ((vm_offset_t)udaddr & PAGE_MASK));
#endif
		lwbuf_free(lwb);
		vm_page_unhold(m);
	}
	if (err)
		err = EFAULT;
	return (err);
}

static int
ept_copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *res)
{
	int error;
	size_t n;
	const char *uptr = udaddr;
	char *kptr = kaddr;

	if (res)
		*res = 0;
	while (len) {
		n = PAGE_SIZE - ((vm_offset_t)uptr & PAGE_MASK);
		if (n > 32)
			n = 32;
		if (n > len)
			n = len;
		if ((error = ept_copyin(uptr, kptr, n)) != 0)
			return(error);
		while (n) {
			if (res)
				++*res;
			if (*kptr == 0)
				return(0);
			++kptr;
			++uptr;
			--n;
			--len;
		}

	}
	return(ENAMETOOLONG);
}


static int
ept_fubyte(const void *base)
{
	unsigned char c = 0;

	if (ept_copyin(base, &c, 1) == 0)
		return((int)c);
	return(-1);
}

static int
ept_subyte(void *base, int byte)
{
	unsigned char c = byte;

	if (ept_copyout(&c, base, 1) == 0)
		return(0);
	return(-1);
}

static long
ept_fuword(const void *base)
{
	long v;

	if (ept_copyin(base, &v, sizeof(v)) == 0)
		return(v);
	return(-1);
}

static int
ept_suword(void *base, long word)
{
	if (ept_copyout(&word, base, sizeof(word)) == 0)
		return(0);
	return(-1);
}

static int
ept_suword32(void *base, int word)
{
	if (ept_copyout(&word, base, sizeof(word)) == 0)
		return(0);
	return(-1);
}

void
vmx_ept_pmap_pinit(pmap_t pmap)
{
	pmap->pm_flags |= pmap_pm_flags_ept;

	bcopy(pmap_bits_ept, pmap->pmap_bits, sizeof(pmap_bits_ept));
	bcopy(ept_protection_codes, pmap->protection_codes,
	      sizeof(ept_protection_codes));
	bcopy(pmap_cache_bits_ept, pmap->pmap_cache_bits,
	      sizeof(pmap_cache_bits_ept));
	pmap->pmap_cache_mask = pmap_cache_mask_ept;
	pmap->copyinstr = ept_copyinstr;
	pmap->copyin = ept_copyin;
	pmap->copyout = ept_copyout;
	pmap->fubyte = ept_fubyte;
	pmap->subyte = ept_subyte;
	pmap->fuword = ept_fuword;
	pmap->suword = ept_suword;
	pmap->suword32 = ept_suword32;
}
