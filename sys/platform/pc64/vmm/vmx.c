/*
 * Copyright (c) 2003-2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Mihai Carabas <mihai.carabas@gmail.com>
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
 */

#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/sysctl.h>
#include <sys/vmm.h>
#include <sys/proc.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/vkernel.h>
#include <sys/mplock2.h>
#include <ddb/ddb.h>

#include <cpu/cpu.h>

#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/smp.h>
#include <machine/globaldata.h>
#include <machine/trap.h>
#include <machine/pmap.h>
#include <machine/md_var.h>

#include <vm/vm_map.h>
#include <vm/vm_extern.h>
#include <vm/vm_param.h>

#include "vmm.h"
#include "vmm_utils.h"

#include "vmx.h"
#include "vmx_instr.h"
#include "vmx_vmcs.h"

#include "ept.h"

extern void trap(struct trapframe *frame);

static int vmx_check_cpu_migration(void);
static int execute_vmptrld(struct vmx_thread_info *vti);

struct instr_decode syscall_asm = {
	.opcode_bytes = 2,
	.opcode.byte1 = 0x0F,
	.opcode.byte2 = 0x05,
};

struct vmx_ctl_info vmx_pinbased = {
	.msr_addr = IA32_VMX_PINBASED_CTLS,
	.msr_true_addr = IA32_VMX_TRUE_PINBASED_CTLS,
};

struct vmx_ctl_info vmx_procbased = {
	.msr_addr = IA32_VMX_PROCBASED_CTLS,
	.msr_true_addr = IA32_VMX_TRUE_PROCBASED_CTLS,
};

struct vmx_ctl_info vmx_procbased2 = {
	.msr_addr = IA32_VMX_PROCBASED_CTLS2,
	.msr_true_addr = IA32_VMX_PROCBASED_CTLS2,
};

struct vmx_ctl_info vmx_exit = {
	.msr_addr = IA32_VMX_EXIT_CTLS,
	.msr_true_addr = IA32_VMX_TRUE_EXIT_CTLS,
};

struct vmx_ctl_info vmx_entry = {
	.msr_addr = IA32_VMX_ENTRY_CTLS,
	.msr_true_addr = IA32_VMX_TRUE_ENTRY_CTLS,
};

/* Declared in generic vmm.c - SYSCTL parent */
extern struct sysctl_oid *vmm_sysctl_tree;

/* SYSCTL tree and context */
static struct sysctl_oid *vmx_sysctl_tree;
static struct sysctl_ctx_list vmx_sysctl_ctx;

/* Per cpu info */
struct vmx_pcpu_info *pcpu_info;

/* VMX BASIC INFO */
uint32_t vmx_revision;
uint32_t vmx_region_size;
uint8_t vmx_width_addr;

/* IA32_VMX_EPT_VPID_CAP */
uint64_t vmx_ept_vpid_cap;

/* VMX fixed bits */
uint64_t cr0_fixed_to_0;
uint64_t cr4_fixed_to_0;
uint64_t cr0_fixed_to_1;
uint64_t cr4_fixed_to_1;

/* VMX status */
static uint8_t vmx_enabled = 0;
static uint8_t vmx_initialized = 0;

/* VMX set control setting
 * Intel System Programming Guide, Part 3, Order Number 326019
 * 31.5.1 Algorithms for Determining VMX Capabilities
 * Implement Algorithm 3
 */
static int
vmx_set_ctl_setting(struct vmx_ctl_info *vmx_ctl, uint32_t bit_no, setting_t value) {
	uint64_t vmx_basic;
	uint64_t ctl_val;

	/* Check if its branch b. or c. */
	vmx_basic = rdmsr(IA32_VMX_BASIC);
	if (IS_TRUE_CTL_AVAIL(vmx_basic))
		ctl_val = rdmsr(vmx_ctl->msr_true_addr);
	else
		ctl_val = rdmsr(vmx_ctl->msr_addr);

	/* Check if the value is known by VMM or set on DEFAULT */
	switch(value) {
		case DEFAULT:
			/* Both settings are allowd
			 * - step b.iii)
			 *   or
			 * - c.iii), c.iv)
			 */
			if (IS_ZERO_SETTING_ALLOWED(ctl_val, bit_no)
			    && IS_ONE_SETTING_ALLOWED(ctl_val, bit_no)) {

				/* For c.iii) and c.iv) */
				if(IS_TRUE_CTL_AVAIL(vmx_basic))
					ctl_val = rdmsr(vmx_ctl->msr_addr);

				if (IS_ZERO_SETTING_ALLOWED(ctl_val, bit_no))
					vmx_ctl->ctls &= ~BIT(bit_no);
				else if (IS_ONE_SETTING_ALLOWED(ctl_val, bit_no))
					vmx_ctl->ctls |= BIT(bit_no);

			} else if (IS_ZERO_SETTING_ALLOWED(ctl_val, bit_no)) {
				/* b.i), c.i) */
				vmx_ctl->ctls &= ~BIT(bit_no);

			} else if (IS_ONE_SETTING_ALLOWED(ctl_val, bit_no)) {
				/* b.i), c.i) */
				vmx_ctl->ctls |= BIT(bit_no);

			} else {
				return (EINVAL);
			}
			break;
		case ZERO:
			/* For b.ii) or c.ii) */
			if (!IS_ZERO_SETTING_ALLOWED(ctl_val, bit_no))
				return (EINVAL);

			vmx_ctl->ctls &= ~BIT(bit_no);

			break;
		case ONE:
			/* For b.ii) or c.ii) */
			if (!IS_ONE_SETTING_ALLOWED(ctl_val, bit_no))
				return (EINVAL);

			vmx_ctl->ctls |= BIT(bit_no);

			break;
	}
	return 0;
}

static void
vmx_set_default_settings(struct vmx_ctl_info *vmx_ctl)
{
	int i;

	for(i = 0; i < 32; i++) {
		vmx_set_ctl_setting(vmx_ctl, i, DEFAULT);
	}
}

static void
alloc_vmxon_regions(void)
{
	int cpu;
	pcpu_info = kmalloc(ncpus * sizeof(struct vmx_pcpu_info), M_TEMP, M_WAITOK | M_ZERO);

	for (cpu = 0; cpu < ncpus; cpu++) {

		/* The address must be aligned to 4K - alloc extra */
		pcpu_info[cpu].vmxon_region_na = kmalloc(vmx_region_size + VMXON_REGION_ALIGN_SIZE,
		    M_TEMP,
		    M_WAITOK | M_ZERO);

		/* Align address */
		pcpu_info[cpu].vmxon_region = (unsigned char*) VMXON_REGION_ALIGN(pcpu_info[cpu].vmxon_region_na);

		/* In the first 31 bits put the vmx revision*/
		*((uint32_t *) pcpu_info[cpu].vmxon_region) = vmx_revision;
	}
}

static void
free_vmxon_regions(void)
{
	int i;

	for (i = 0; i < ncpus; i++) {
		pcpu_info[i].vmxon_region = NULL;

		kfree(pcpu_info[i].vmxon_region_na, M_TEMP);
	}

	kfree(pcpu_info, M_TEMP);
}

static void
build_vmx_sysctl(void)
{
	sysctl_ctx_init(&vmx_sysctl_ctx);
	vmx_sysctl_tree = SYSCTL_ADD_NODE(&vmx_sysctl_ctx,
		    SYSCTL_CHILDREN(vmm_sysctl_tree),
		    OID_AUTO, "vmx",
		    CTLFLAG_RD, 0, "VMX options");

	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "revision", CTLFLAG_RD,
	    &vmx_revision, 0,
	    "VMX revision");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "region_size", CTLFLAG_RD,
	    &vmx_region_size, 0,
	    "VMX region size");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "width_addr", CTLFLAG_RD,
	    &vmx_width_addr, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "pinbased_ctls", CTLFLAG_RD,
	    &vmx_pinbased.ctls, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "procbased_ctls", CTLFLAG_RD,
	    &vmx_procbased.ctls, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "procbased2_ctls", CTLFLAG_RD,
	    &vmx_procbased2.ctls, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "vmexit_ctls", CTLFLAG_RD,
	    &vmx_exit.ctls, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "vmentry_ctls", CTLFLAG_RD,
	    &vmx_entry.ctls, 0,
	    "VMX width address");
	SYSCTL_ADD_INT(&vmx_sysctl_ctx,
	    SYSCTL_CHILDREN(vmx_sysctl_tree),
	    OID_AUTO, "ept_vpid_cap", CTLFLAG_RD,
	    &vmx_ept_vpid_cap, 0,
	    "VMX EPT VPID CAP");
}



static int
vmx_init(void)
{
	uint64_t feature_control;
	uint64_t vmx_basic_value;
	uint64_t cr0_fixed_bits_to_1;
	uint64_t cr0_fixed_bits_to_0;
	uint64_t cr4_fixed_bits_to_0;
	uint64_t cr4_fixed_bits_to_1;

	int err;


	/*
	 * The ability of a processor to support VMX operation
	 * and related instructions is indicated by:
	 * CPUID.1:ECX.VMX[bit 5] = 1
	 */
	if (!(cpu_feature2 & CPUID2_VMX)) {
		kprintf("VMM: VMX is not supported by this Intel CPU\n");
		return (ENODEV);
	}

	vmx_set_default_settings(&vmx_pinbased);

	vmx_set_default_settings(&vmx_procbased);
	/* Enable second level for procbased */
	err = vmx_set_ctl_setting(&vmx_procbased,
	    PROCBASED_ACTIVATE_SECONDARY_CONTROLS,
	    ONE);
	if (err) {
		kprintf("VMM: PROCBASED_ACTIVATE_SECONDARY_CONTROLS not supported by this CPU\n");
		return (ENODEV);
	}
	vmx_set_default_settings(&vmx_procbased2);

	vmx_set_default_settings(&vmx_exit);
	vmx_set_default_settings(&vmx_entry);

	/* Enable external interrupts exiting */
	err = vmx_set_ctl_setting(&vmx_pinbased,
	    PINBASED_EXTERNAL_INTERRUPT_EXITING,
	    ONE);
	if (err) {
		kprintf("VMM: PINBASED_EXTERNAL_INTERRUPT_EXITING not supported by this CPU\n");
		return (ENODEV);
	}

	/* Enable non-maskable interrupts exiting */
	err = vmx_set_ctl_setting(&vmx_pinbased,
	    PINBASED_NMI_EXITING,
	    ONE);
	if (err) {
		kprintf("VMM: PINBASED_NMI_EXITING not supported by this CPU\n");
		return (ENODEV);
	}


	/* Set 64bits mode for GUEST */
	err = vmx_set_ctl_setting(&vmx_entry,
	    VMENTRY_IA32e_MODE_GUEST,
	    ONE);
	if (err) {
		kprintf("VMM: VMENTRY_IA32e_MODE_GUEST not supported by this CPU\n");
		return (ENODEV);
	}

	/* Load MSR EFER on enry */
	err = vmx_set_ctl_setting(&vmx_entry,
	    VMENTRY_LOAD_IA32_EFER,
	    ONE);
	if (err) {
		kprintf("VMM: VMENTRY_LOAD_IA32_EFER not supported by this CPU\n");
		return (ENODEV);
	}

	/* Set 64bits mode */
	err = vmx_set_ctl_setting(&vmx_exit,
	    VMEXIT_HOST_ADDRESS_SPACE_SIZE,
	    ONE);
	if (err) {
		kprintf("VMM: VMEXIT_HOST_ADDRESS_SPACE_SIZE not supported by this CPU\n");
		return (ENODEV);
	}

	/* Save/Load Efer on exit */
	err = vmx_set_ctl_setting(&vmx_exit,
	    VMEXIT_SAVE_IA32_EFER,
	    ONE);
	if (err) {
		kprintf("VMM: VMEXIT_SAVE_IA32_EFER not supported by this CPU\n");
		return (ENODEV);
	}

	/* Load Efer on exit */
	err = vmx_set_ctl_setting(&vmx_exit,
	    VMEXIT_LOAD_IA32_EFER,
	    ONE);
	if (err) {
		kprintf("VMM: VMEXIT_LOAD_IA32_EFER not supported by this CPU\n");
		return (ENODEV);
	}

	/* Enable EPT feature */
	err = vmx_set_ctl_setting(&vmx_procbased2,
	    PROCBASED2_ENABLE_EPT,
	    ONE);
	if (err) {
		kprintf("VMM: PROCBASED2_ENABLE_EPT not supported by this CPU\n");
		return (ENODEV);
	}

	if (vmx_ept_init()) {
		kprintf("VMM: vmx_ept_init failed\n");
		return (ENODEV);
	}
#if 0
	/* XXX - to implement in the feature */
	/* Enable VPID feature */
	err = vmx_set_ctl_setting(&vmx_procbased2,
	    PROCBASED2_ENABLE_VPID,
	    ONE);
	if (err) {
		kprintf("VMM: PROCBASED2_ENABLE_VPID not supported by this CPU\n");
		return (ENODEV);
	}
#endif

	/* Check for the feature control status */
	feature_control = rdmsr(IA32_FEATURE_CONTROL);
	if (!(feature_control & BIT(FEATURE_CONTROL_LOCKED))) {
		kprintf("VMM: IA32_FEATURE_CONTROL is not locked\n");
		return (EINVAL);
	}
	if (!(feature_control & BIT(FEATURE_CONTROL_VMX_BIOS_ENABLED))) {
		kprintf("VMM: VMX is disabled by the BIOS\n");
		return (EINVAL);
	}

	vmx_basic_value = rdmsr(IA32_VMX_BASIC);
	vmx_width_addr = (uint8_t) VMX_WIDTH_ADDR(vmx_basic_value);
	vmx_region_size = (uint32_t) VMX_REGION_SIZE(vmx_basic_value);
	vmx_revision = (uint32_t) VMX_REVISION(vmx_basic_value);

	/* A.7 VMX-FIXED BITS IN CR0 */
	cr0_fixed_bits_to_1 = rdmsr(IA32_VMX_CR0_FIXED0);
	cr0_fixed_bits_to_0 = rdmsr(IA32_VMX_CR0_FIXED1);
	cr0_fixed_to_1 = cr0_fixed_bits_to_1 & cr0_fixed_bits_to_0;
	cr0_fixed_to_0 = ~cr0_fixed_bits_to_1 & ~cr0_fixed_bits_to_0;

	/* A.8 VMX-FIXED BITS IN CR4 */
	cr4_fixed_bits_to_1 = rdmsr(IA32_VMX_CR4_FIXED0);
	cr4_fixed_bits_to_0 = rdmsr(IA32_VMX_CR4_FIXED1);
	cr4_fixed_to_1 = cr4_fixed_bits_to_1 & cr4_fixed_bits_to_0;
	cr4_fixed_to_0 = ~cr4_fixed_bits_to_1 & ~cr4_fixed_bits_to_0;

	build_vmx_sysctl();

	vmx_initialized = 1;
	return 0;
}

static void
execute_vmxon(void *perr)
{
	unsigned char *vmxon_region;
	int *err = (int*) perr;

	/* A.7 VMX-FIXED BITS IN CR0 */
	load_cr0((rcr0() | cr0_fixed_to_1) & ~cr0_fixed_to_0);

	/* A.8 VMX-FIXED BITS IN CR4 */
	load_cr4((rcr4() | cr4_fixed_to_1) & ~cr4_fixed_to_0);

	/* Enable VMX */
	load_cr4(rcr4() | CR4_VMXE);

	vmxon_region = pcpu_info[mycpuid].vmxon_region;
	*err = vmxon(vmxon_region);
	if (*err) {
		kprintf("VMM: vmxon failed on cpu%d\n", mycpuid);
	}
}

static void
execute_vmxoff(void *dummy)
{
	invept_desc_t desc = { 0 };

	if (invept(INVEPT_TYPE_ALL_CONTEXTS, (uint64_t*) &desc))
		kprintf("VMM: execute_vmxoff: invet failed on cpu%d\n", mycpu->gd_cpuid);

	vmxoff();

	/* Disable VMX */
	load_cr4(rcr4() & ~CR4_VMXE);
}

static void
execute_vmclear(void *data)
{
	struct vmx_thread_info *vti = data;
	int err;
	globaldata_t gd = mycpu;

	if (pcpu_info[gd->gd_cpuid].loaded_vmx == vti) {
		/*
		 * Must set vti->launched to zero after vmclear'ing to
		 * force a vmlaunch the next time.
		 *
		 * Must not clear the loaded_vmx field until after we call
		 * vmclear on the region.  This field triggers the interlocked
		 * cpusync from another cpu trying to destroy or reuse
		 * the vti.  If we clear the field first, the other cpu will
		 * not interlock and may race our vmclear() on the underlying
		 * memory.
		 */
		ERROR_IF(vmclear(vti->vmcs_region));
error:
		pcpu_info[gd->gd_cpuid].loaded_vmx = NULL;
		vti->launched = 0;
	}
	return;
}

static int
execute_vmptrld(struct vmx_thread_info *vti)
{
	globaldata_t gd = mycpu;

	/*
	 * Must vmclear previous active vcms if it is different.
	 */
	if (pcpu_info[gd->gd_cpuid].loaded_vmx &&
	    pcpu_info[gd->gd_cpuid].loaded_vmx != vti)
		execute_vmclear(pcpu_info[gd->gd_cpuid].loaded_vmx);

	/*
	 * Make this the current VMCS.  Must set loaded_vmx field
	 * before calling vmptrld() to avoid races against cpusync.
	 *
	 * Must set vti->launched to zero after the vmptrld to force
	 * a vmlaunch.
	 */
	if (pcpu_info[gd->gd_cpuid].loaded_vmx != vti) {
		vti->launched = 0;
		pcpu_info[gd->gd_cpuid].loaded_vmx = vti;
		return (vmptrld(vti->vmcs_region));
	} else {
		return (0);
	}
}

static int
vmx_enable(void)
{
	int err;
	int cpu;

	if (!vmx_initialized) {
		kprintf("VMM: vmx_enable - not allowed; vmx not initialized\n");
		return (EINVAL);
	}

	if (vmx_enabled) {
		kprintf("VMM: vmx_enable - already enabled\n");
		return (EINVAL);
	}

	alloc_vmxon_regions();
	for (cpu = 0; cpu < ncpus; cpu++) {
		err = 0;
		lwkt_cpusync_simple(CPUMASK(cpu), execute_vmxon, &err);
		if(err) {
			kprintf("VMM: vmx_enable error %d on cpu%d\n", err, cpu);
			return err;
		}
	}
	vmx_enabled = 1;
	return 0;
}

static int
vmx_disable(void)
{
	int cpu;

	if (!vmx_enabled) {
		kprintf("VMM: vmx_disable not allowed; vmx wasn't enabled\n");
	}

	for (cpu = 0; cpu < ncpus; cpu++)
		lwkt_cpusync_simple(CPUMASK(cpu), execute_vmxoff, NULL);

	free_vmxon_regions();

	vmx_enabled = 0;

	return 0;
}

static int vmx_set_guest_descriptor(descriptor_t type,
		uint16_t selector,
		uint32_t rights,
		uint64_t base,
		uint32_t limit)
{
	int err;
	int selector_enc;
	int rights_enc;
	int base_enc;
	int limit_enc;


	/*
	 * Intel Manual Vol 3C. - page 60
	 * If any bit in the limit field in the range 11:0 is 0, G must be 0.
	 * If any bit in the limit field in the range 31:20 is 1, G must be 1.
	 */
	if ((~rights & VMCS_SEG_UNUSABLE) || (type == CS)) {
		if ((limit & 0xfff) != 0xfff)
			rights &= ~VMCS_G;
		else if ((limit & 0xfff00000) != 0)
			rights |= VMCS_G;
	}

	switch(type) {
		case ES:
			selector_enc = VMCS_GUEST_ES_SELECTOR;
			rights_enc = VMCS_GUEST_ES_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_ES_BASE;
			limit_enc = VMCS_GUEST_ES_LIMIT;
			break;
		case CS:
			selector_enc = VMCS_GUEST_CS_SELECTOR;
			rights_enc = VMCS_GUEST_CS_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_CS_BASE;
			limit_enc = VMCS_GUEST_CS_LIMIT;
			break;
		case SS:
			selector_enc = VMCS_GUEST_SS_SELECTOR;
			rights_enc = VMCS_GUEST_SS_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_SS_BASE;
			limit_enc = VMCS_GUEST_SS_LIMIT;
			break;
		case DS:
			selector_enc = VMCS_GUEST_DS_SELECTOR;
			rights_enc = VMCS_GUEST_DS_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_DS_BASE;
			limit_enc = VMCS_GUEST_DS_LIMIT;
			break;
		case FS:
			selector_enc = VMCS_GUEST_FS_SELECTOR;
			rights_enc = VMCS_GUEST_FS_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_FS_BASE;
			limit_enc = VMCS_GUEST_FS_LIMIT;
			break;
		case GS:
			selector_enc = VMCS_GUEST_GS_SELECTOR;
			rights_enc = VMCS_GUEST_GS_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_GS_BASE;
			limit_enc = VMCS_GUEST_GS_LIMIT;
			break;
		case LDTR:
			selector_enc = VMCS_GUEST_LDTR_SELECTOR;
			rights_enc = VMCS_GUEST_LDTR_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_LDTR_BASE;
			limit_enc = VMCS_GUEST_LDTR_LIMIT;
			break;
		case TR:
			selector_enc = VMCS_GUEST_TR_SELECTOR;
			rights_enc = VMCS_GUEST_TR_ACCESS_RIGHTS;
			base_enc = VMCS_GUEST_TR_BASE;
			limit_enc = VMCS_GUEST_TR_LIMIT;
			break;
		default:
			kprintf("VMM: vmx_set_guest_descriptor: unknown descriptor\n");
			err = -1;
			goto error;
			break;
	}

	ERROR_IF(vmwrite(selector_enc, selector));
	ERROR_IF(vmwrite(rights_enc, rights));
	ERROR_IF(vmwrite(base_enc, base));
	ERROR_IF(vmwrite(limit_enc, limit));

	return 0;
error:
	kprintf("VMM: vmx_set_guest_descriptor failed\n");
	return err;
}

/*
 * Called by the first thread of the VMM process
 * - create a new vmspace
 * - init the vmspace with EPT PG_* bits and
 *   EPT copyin/copyout functions
 * - replace the vmspace of the current proc
 * - remove the old vmspace
 */
static int
vmx_vminit_master(struct vmm_guest_options *options)
{
	struct vmspace *oldvmspace;
	struct vmspace *newvmspace;
	struct proc *p = curthread->td_proc;
	struct vmm_proc *p_vmm;

	oldvmspace = curthread->td_lwp->lwp_vmspace;
	newvmspace = vmspace_fork(oldvmspace);

	vmx_ept_pmap_pinit(vmspace_pmap(newvmspace));
	bzero(vmspace_pmap(newvmspace)->pm_pml4, PAGE_SIZE);

	lwkt_gettoken(&oldvmspace->vm_map.token);
	lwkt_gettoken(&newvmspace->vm_map.token);

	pmap_pinit2(vmspace_pmap(newvmspace));
	pmap_replacevm(curthread->td_proc, newvmspace, 0);

	lwkt_reltoken(&newvmspace->vm_map.token);
	lwkt_reltoken(&oldvmspace->vm_map.token);

	vmspace_free(oldvmspace);

	options->vmm_cr3 = vtophys(vmspace_pmap(newvmspace)->pm_pml4);

	p_vmm = kmalloc(sizeof(struct vmm_proc), M_TEMP, M_WAITOK | M_ZERO);
	p_vmm->guest_cr3 = options->guest_cr3;
	p_vmm->vmm_cr3 = options->vmm_cr3;
	p->p_vmm = (void *)p_vmm;

	if (p->p_vkernel) {
		p->p_vkernel->vkernel_cr3 = options->guest_cr3;
		dkprintf("PROCESS CR3 %016jx\n", (intmax_t)options->guest_cr3);
	}

	return 0;
}

static int
vmx_vminit(struct vmm_guest_options *options)
{
	struct vmx_thread_info * vti;
	int err;
	struct tls_info guest_fs = curthread->td_tls.info[0];
	struct tls_info guest_gs = curthread->td_tls.info[1];


	vti = kmalloc(sizeof(struct vmx_thread_info), M_TEMP, M_WAITOK | M_ZERO);
	curthread->td_vmm = (void*) vti;

	if (options->master) {
		vmx_vminit_master(options);
	}

	bcopy(&options->tf, &vti->guest, sizeof(struct trapframe));

	/*
	 * Be sure we return success if the VMM hook enters
	 */
	vti->guest.tf_rax = 0;
	vti->guest.tf_rflags &= ~PSL_C;

	vti->vmcs_region_na = kmalloc(vmx_region_size + VMXON_REGION_ALIGN_SIZE,
		    M_TEMP,
		    M_WAITOK | M_ZERO);

	/* Align address */
	vti->vmcs_region = (unsigned char*) VMXON_REGION_ALIGN(vti->vmcs_region_na);
	vti->last_cpu = -1;

	vti->guest_cr3 = options->guest_cr3;
	vti->vmm_cr3 = options->vmm_cr3;

	/* In the first 31 bits put the vmx revision*/
	*((uint32_t *)vti->vmcs_region) = vmx_revision;

	/*
	 * vmclear the vmcs to initialize it.
	 */
	ERROR_IF(vmclear(vti->vmcs_region));

	crit_enter();

	ERROR_IF(execute_vmptrld(vti));

	/* Load the VMX controls */
	ERROR_IF(vmwrite(VMCS_PINBASED_CTLS, vmx_pinbased.ctls));
	ERROR_IF(vmwrite(VMCS_PROCBASED_CTLS, vmx_procbased.ctls));
	ERROR_IF(vmwrite(VMCS_PROCBASED2_CTLS, vmx_procbased2.ctls));
	ERROR_IF(vmwrite(VMCS_VMEXIT_CTLS, vmx_exit.ctls));
	ERROR_IF(vmwrite(VMCS_VMENTRY_CTLS, vmx_entry.ctls));

	/* Load HOST CRs */
	ERROR_IF(vmwrite(VMCS_HOST_CR0, rcr0()));
	ERROR_IF(vmwrite(VMCS_HOST_CR4, rcr4()));

	/* Load HOST EFER and PAT */
//	ERROR_IF(vmwrite(VMCS_HOST_IA32_PAT, rdmsr(MSR_PAT)));
	ERROR_IF(vmwrite(VMCS_HOST_IA32_EFER, rdmsr(MSR_EFER)));

	/* Load HOST selectors */
	ERROR_IF(vmwrite(VMCS_HOST_ES_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_IF(vmwrite(VMCS_HOST_SS_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_IF(vmwrite(VMCS_HOST_FS_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_IF(vmwrite(VMCS_HOST_GS_SELECTOR, GSEL(GDATA_SEL, SEL_KPL)));
	ERROR_IF(vmwrite(VMCS_HOST_CS_SELECTOR, GSEL(GCODE_SEL, SEL_KPL)));
	ERROR_IF(vmwrite(VMCS_HOST_TR_SELECTOR, GSEL(GPROC0_SEL, SEL_KPL)));

	/*
	 * The BASE addresses are written on each VMRUN in case
	 * the CPU changes because are per-CPU values
	 */

	/*
	 * Call vmx_vmexit on VM_EXIT condition
	 * The RSP will point to the vmx_thread_info
	 */
	ERROR_IF(vmwrite(VMCS_HOST_RIP, (uint64_t) vmx_vmexit));
	ERROR_IF(vmwrite(VMCS_HOST_RSP, (uint64_t) vti));
	ERROR_IF(vmwrite(VMCS_HOST_CR3, (uint64_t) KPML4phys));

	/*
	 * GUEST initialization
	 * - set the descriptors according the conditions from Intel
	 *   manual "26.3.1.2 Checks on Guest Segment Registers
	 * - set the privilege to SEL_UPL (the vkernel will run
	 *   in userspace context)
	 */
	ERROR_IF(vmx_set_guest_descriptor(ES, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    0, 0));

	ERROR_IF(vmx_set_guest_descriptor(SS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    0, 0));

	ERROR_IF(vmx_set_guest_descriptor(DS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    0, 0));

	ERROR_IF(vmx_set_guest_descriptor(FS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    (uint64_t) guest_fs.base, (uint32_t) guest_fs.size));

	ERROR_IF(vmx_set_guest_descriptor(GS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    (uint64_t) guest_gs.base, (uint32_t) guest_gs.size));

	ERROR_IF(vmx_set_guest_descriptor(CS, GSEL(GUCODE_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(11) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P | VMCS_L,
	    0, 0));

	ERROR_IF(vmx_set_guest_descriptor(TR, GSEL(GPROC0_SEL, SEL_UPL),
			VMCS_SEG_TYPE(11) | VMCS_P,
			0, 0));

	ERROR_IF(vmx_set_guest_descriptor(LDTR, 0, VMCS_SEG_UNUSABLE, 0, 0));

	/* Set the CR0/CR4 registers, removing the unsupported bits */
	ERROR_IF(vmwrite(VMCS_GUEST_CR0, (CR0_PE | CR0_PG |
	    cr0_fixed_to_1) & ~cr0_fixed_to_0));
	ERROR_IF(vmwrite(VMCS_GUEST_CR4, (CR4_PAE | CR4_FXSR | CR4_XMM | CR4_XSAVE |
	    cr4_fixed_to_1) & ~ cr4_fixed_to_0));

	/* Don't set EFER_SCE for catching "syscall" instructions */
	ERROR_IF(vmwrite(VMCS_GUEST_IA32_EFER, (EFER_LME | EFER_LMA)));

	vti->guest.tf_rflags = PSL_I | 0x02;
	ERROR_IF(vmwrite(VMCS_GUEST_RFLAGS, vti->guest.tf_rflags));

	/* The Guest CR3 indicating CR3 pagetable */
	ERROR_IF(vmwrite(VMCS_GUEST_CR3, (uint64_t) vti->guest_cr3));

	/* Throw all possible exceptions */
	ERROR_IF(vmwrite(VMCS_EXCEPTION_BITMAP,(uint64_t) 0xFFFFFFFF));

	/* Guest RIP and RSP */
	ERROR_IF(vmwrite(VMCS_GUEST_RIP, options->tf.tf_rip));
	ERROR_IF(vmwrite(VMCS_GUEST_RSP, options->tf.tf_rsp));

	/*
	 * This field is included for future expansion.
	 * Software should set this field to FFFFFFFF_FFFFFFFFH
	 * to avoid VM-entry failures (see Section 26.3.1.5).
	 */
	ERROR_IF(vmwrite(VMCS_LINK_POINTER, ~0ULL));

	/* The pointer to the EPT pagetable */
	ERROR_IF(vmwrite(VMCS_EPTP, vmx_eptp(vti->vmm_cr3)));

	vti->invept_desc.eptp = vmx_eptp(vti->vmm_cr3);

	crit_exit();

	return 0;
error:
	crit_exit();

	kprintf("VMM: vmx_vminit failed\n");
	execute_vmclear(vti);

	kfree(vti->vmcs_region_na, M_TEMP);
	kfree(vti, M_TEMP);
	return err;
}

static int
vmx_vmdestroy(void)
{
	struct vmx_thread_info *vti = curthread->td_vmm;
	struct proc *p = curproc;
	int error = -1;

	if (vti != NULL) {
		vmx_check_cpu_migration();
		if (vti->vmcs_region &&
		    pcpu_info[mycpu->gd_cpuid].loaded_vmx == vti)
			execute_vmclear(vti);

		if (vti->vmcs_region_na != NULL) {
			kfree(vti->vmcs_region_na, M_TEMP);
			kfree(vti, M_TEMP);
			error = 0;
		}
		curthread->td_vmm = NULL;
		lwkt_gettoken(&p->p_token);
		if (p->p_nthreads == 1) {
			kfree(p->p_vmm, M_TEMP);
			p->p_vmm = NULL;
		}
	}
	return error;
}

/*
 * Checks if we migrated to another cpu
 *
 * No locks are required
 */
static int
vmx_check_cpu_migration(void)
{
	struct vmx_thread_info * vti;
	struct globaldata *gd;
	int err;

	gd = mycpu;
	vti = (struct vmx_thread_info *) curthread->td_vmm;
	ERROR_IF(vti == NULL);

	if (vti->last_cpu != -1 && vti->last_cpu != gd->gd_cpuid &&
	    pcpu_info[vti->last_cpu].loaded_vmx == vti) {
		/*
		 * Do not reset last_cpu to -1 here, leave it caching
		 * the cpu whos per-cpu fields the VMCS is synchronized
		 * with.  The pcpu_info[] check prevents unecessary extra
		 * cpusyncs.
		 */
		dkprintf("VMM: cpusync from %d to %d\n", gd->gd_cpuid, vti->last_cpu);

		/* Clear the VMCS area if ran on another CPU */
		lwkt_cpusync_simple(CPUMASK(vti->last_cpu),
				    execute_vmclear, (void *)vti);
	}
	return 0;
error:
	kprintf("VMM: vmx_check_cpu_migration failed\n");
	return err;
}

/* Handle CPU migration
 *
 * We have to enter with interrupts disabled/critical section
 * to be sure that another VMCS won't steel our CPU.
 */
static inline int
vmx_handle_cpu_migration(void)
{
	struct vmx_thread_info * vti;
	struct globaldata *gd;
	int err;

	gd = mycpu;
	vti = (struct vmx_thread_info *) curthread->td_vmm;
	ERROR_IF(vti == NULL);

	if (vti->last_cpu != gd->gd_cpuid) {
		/*
		 * We need to synchronize the per-cpu fields after changing
		 * cpus.
		 */
		dkprintf("VMM: vmx_handle_cpu_migration init per CPU data\n");

		ERROR_IF(execute_vmptrld(vti));

		/* Host related registers */
		ERROR_IF(vmwrite(VMCS_HOST_GS_BASE, (uint64_t) gd)); /* mycpu points to %gs:0 */
		ERROR_IF(vmwrite(VMCS_HOST_TR_BASE, (uint64_t) &gd->gd_prvspace->mdglobaldata.gd_common_tss));

		ERROR_IF(vmwrite(VMCS_HOST_GDTR_BASE, (uint64_t) &gdt[gd->gd_cpuid * NGDT]));
		ERROR_IF(vmwrite(VMCS_HOST_IDTR_BASE, (uint64_t) r_idt_arr[gd->gd_cpuid].rd_base));


		/* Guest related register */
		ERROR_IF(vmwrite(VMCS_GUEST_GDTR_BASE, (uint64_t) &gdt[gd->gd_cpuid * NGDT]));
		ERROR_IF(vmwrite(VMCS_GUEST_GDTR_LIMIT, (uint64_t) (NGDT * sizeof(gdt[0]) - 1)));

		/*
		 * Indicates which cpu the per-cpu fields are synchronized
		 * with.  Does not indicate whether the vmcs is active on
		 * that particular cpu.
		 */
		vti->last_cpu = gd->gd_cpuid;
	} else if (pcpu_info[gd->gd_cpuid].loaded_vmx != vti) {
		/*
		 * We only need to vmptrld
		 */
		dkprintf("VMM: vmx_handle_cpu_migration: vmcs is not loaded\n");

		ERROR_IF(execute_vmptrld(vti));

	} /* else we don't need to do anything */
	return 0;
error:
	kprintf("VMM: vmx_handle_cpu_migration failed\n");
	return err;
}

/* Load information about VMexit
 *
 * We still are with interrupts disabled/critical secion
 * because we must operate with the VMCS on the CPU
 */
static inline int
vmx_vmexit_loadinfo(void)
{
	struct vmx_thread_info *vti;
	int err;

	vti = (struct vmx_thread_info *) curthread->td_vmm;
	ERROR_IF(vti == NULL);

	ERROR_IF(vmread(VMCS_VMEXIT_REASON, &vti->vmexit_reason));
	ERROR_IF(vmread(VMCS_EXIT_QUALIFICATION, &vti->vmexit_qualification));
	ERROR_IF(vmread(VMCS_VMEXIT_INTERRUPTION_INFO, &vti->vmexit_interruption_info));
	ERROR_IF(vmread(VMCS_VMEXIT_INTERRUPTION_ERROR, &vti->vmexit_interruption_error));
	ERROR_IF(vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH, &vti->vmexit_instruction_length));
	ERROR_IF(vmread(VMCS_GUEST_PHYSICAL_ADDRESS, &vti->guest_physical_address));
	ERROR_IF(vmread(VMCS_GUEST_RIP, &vti->guest.tf_rip));
	ERROR_IF(vmread(VMCS_GUEST_CS_SELECTOR, &vti->guest.tf_cs));
	ERROR_IF(vmread(VMCS_GUEST_RFLAGS, &vti->guest.tf_rflags));
	ERROR_IF(vmread(VMCS_GUEST_RSP, &vti->guest.tf_rsp));
	ERROR_IF(vmread(VMCS_GUEST_SS_SELECTOR, &vti->guest.tf_ss));

	return 0;
error:
	kprintf("VMM: vmx_vmexit_loadinfo failed\n");
	return err;
}


static int
vmx_set_tls_area(void)
{
	struct tls_info *guest_fs = &curthread->td_tls.info[0];
	struct tls_info *guest_gs = &curthread->td_tls.info[1];

	int err;

	dkprintf("VMM: vmx_set_tls_area hook\n");

	crit_enter();

	ERROR_IF(vmx_check_cpu_migration());
	ERROR_IF(vmx_handle_cpu_migration());

	/* set %fs */
	ERROR_IF(vmx_set_guest_descriptor(FS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    (uint64_t) guest_fs->base, (uint32_t) guest_fs->size));

	/* set %gs */
	ERROR_IF(vmx_set_guest_descriptor(GS, GSEL(GUDATA_SEL, SEL_UPL),
	    VMCS_SEG_TYPE(3) | VMCS_S | VMCS_DPL(SEL_UPL) | VMCS_P,
	    (uint64_t) guest_gs->base, (uint32_t) guest_gs->size));

	crit_exit();
	return 0;

error:
	crit_exit();
	return err;
}


static int
vmx_handle_vmexit(void)
{
	struct vmx_thread_info * vti;
	int exit_reason;
	int exception_type;
	int exception_number;
	int err;
	int func, regs[4];
	int fault_type, rv;
	int fault_flags = 0;
	struct lwp *lp = curthread->td_lwp;

	dkprintf("VMM: handle_vmx_vmexit\n");
	vti = (struct vmx_thread_info *) curthread->td_vmm;
	ERROR_IF(vti == NULL);

	exit_reason = VMCS_BASIC_EXIT_REASON(vti->vmexit_reason);
	switch (exit_reason) {
		case EXIT_REASON_EXCEPTION:
			dkprintf("VMM: handle_vmx_vmexit: EXIT_REASON_EXCEPTION with qualification "
			    "%llx, interruption info %llx, interruption error %llx, instruction "
			    "length %llx\n",
			    (long long) vti->vmexit_qualification,
			    (long long) vti->vmexit_interruption_info,
			    (long long) vti->vmexit_interruption_error,
			    (long long) vti->vmexit_instruction_length);

			dkprintf("VMM: handle_vmx_vmexit: rax: %llx, rip: %llx, "
			    "rsp: %llx,  rdi: %llx, rsi: %llx, %d, vti: %p, master: %p\n",
			    (long long)vti->guest.tf_rax,
			    (long long)vti->guest.tf_rip,
			    (long long)vti->guest.tf_rsp,
			    (long long)vti->guest.tf_rdi,
			    (long long)vti->guest.tf_rsi, exit_reason, vti, curproc->p_vmm);

			exception_type = VMCS_EXCEPTION_TYPE(vti->vmexit_interruption_info);
			exception_number = VMCS_EXCEPTION_NUMBER(vti->vmexit_interruption_info);

			if (exception_type == VMCS_EXCEPTION_HARDWARE) {
				switch (exception_number) {
					case IDT_UD:
						/*
						 * Disabled "syscall" instruction and
						 * now we catch it for executing
						 */
						dkprintf("VMM: handle_vmx_vmexit: VMCS_EXCEPTION_HARDWARE IDT_UD\n");
#ifdef VMM_DEBUG
						/* Check to see if its syscall asm instuction */
						uint8_t instr[INSTRUCTION_MAX_LENGTH];
						if (copyin((const void *) vti->guest.tf_rip, instr, vti->vmexit_instruction_length) &&
						    instr_check(&syscall_asm,(void *) instr, (uint8_t) vti->vmexit_instruction_length)) {
							kprintf("VMM: handle_vmx_vmexit: UD different from syscall: ");
							db_disasm((db_addr_t) instr, FALSE, NULL);
						}
#endif
						/* Called to force a VMEXIT and invalidate TLB */
						if (vti->guest.tf_rax == -1) {
							vti->guest.tf_rip += vti->vmexit_instruction_length;
							break;
						}

						vti->guest.tf_err = 2;
						vti->guest.tf_trapno = T_FAST_SYSCALL;
						vti->guest.tf_xflags = 0;

						vti->guest.tf_rip += vti->vmexit_instruction_length;

						syscall2(&vti->guest);

						break;
					case IDT_PF:
						dkprintf("VMM: handle_vmx_vmexit: VMCS_EXCEPTION_HARDWARE IDT_PF at %llx\n",
						    (long long) vti->guest.tf_rip);

						if (vti->guest.tf_rip == 0) {
							kprintf("VMM: handle_vmx_vmexit: Terminating...\n");
							err = -1;
							goto error;
						}

						vti->guest.tf_err = vti->vmexit_interruption_error;
						vti->guest.tf_addr = vti->vmexit_qualification;
						vti->guest.tf_xflags = 0;
						vti->guest.tf_trapno = T_PAGEFLT;

						/*
						 * If we are a user process in the vkernel
						 * pass the PF to the vkernel and will trigger
						 * the user_trap()
						 *
						 * If we are the vkernel, send a SIGSEGV signal
						 * to us that will trigger the execution of
						 * kern_trap()
						 *
						 */

						if (lp->lwp_vkernel && lp->lwp_vkernel->ve) {
							vkernel_trap(lp, &vti->guest);
						} else {
							trapsignal(lp, SIGSEGV, SEGV_MAPERR);
						}

						break;
					default:
						kprintf("VMM: handle_vmx_vmexit: VMCS_EXCEPTION_HARDWARE unknown "
						    "number %d rip: %llx, rsp: %llx\n", exception_number,
						    (long long)vti->guest.tf_rip, (long long)vti->guest.tf_rsp);
						err = -1;
						goto error;
				}
			} else if (exception_type == VMCS_EXCEPTION_SOFTWARE) {
				switch (exception_number) {
					case 3:
						dkprintf("VMM: handle_vmx_vmexit: VMCS_EXCEPTION_SOFTWARE "
						    "number %d rip: %llx, rsp: %llx\n", exception_number,
						    (long long)vti->guest.tf_rip, (long long)vti->guest.tf_rsp);

						vti->guest.tf_trapno = T_BPTFLT;
						vti->guest.tf_xflags = 0;
						vti->guest.tf_err = 0;
						vti->guest.tf_addr = 0;

						vti->guest.tf_rip += vti->vmexit_instruction_length;

						trap(&vti->guest);

						break;
					default:
						kprintf("VMM: handle_vmx_vmexit: VMCS_EXCEPTION_SOFTWARE unknown "
						    "number %d rip: %llx, rsp: %llx\n", exception_number,
						    (long long)vti->guest.tf_rip, (long long)vti->guest.tf_rsp);
						err = -1;
						goto error;
				}
			} else {
				kprintf("VMM: handle_vmx_vmexit: VMCS_EXCEPTION_ %d unknown\n", exception_type);
				err = -1;
				goto error;
			}
			break;
		case EXIT_REASON_EXT_INTR:
			dkprintf("VMM: handle_vmx_vmexit: EXIT_REASON_EXT_INTR\n");
			break;
		case EXIT_REASON_CPUID:
			dkprintf("VMM: handle_vmx_vmexit: EXIT_REASON_CPUID\n");

			/*
			 * Execute CPUID instruction and pass
			 * the result to the vkernel
			 */

			func = vti->guest.tf_rax;
			do_cpuid(func, regs);

			vti->guest.tf_rax = regs[0];
			vti->guest.tf_rbx = regs[1];
			vti->guest.tf_rcx = regs[2];
			vti->guest.tf_rdx = regs[3];

			vti->guest.tf_rip += vti->vmexit_instruction_length;

			break;
		case EXIT_REASON_EPT_FAULT:
			/*
			 * EPT_FAULT are resolved like normal PFs. Nothing special
			 * - get the fault type
			 * - get the fault address (which is a GPA)
			 * - execute vm_fault on the vm_map
			 */
			dkprintf("VMM: handle_vmx_vmexit: EXIT_REASON_EPT_FAULT with qualification %lld,"
			    "GPA: %llx, fault_Type: %d\n",(long long) vti->vmexit_qualification,
			    (unsigned long long) vti->guest_physical_address, fault_type);

			fault_type = vmx_ept_fault_type(vti->vmexit_qualification);

			if (fault_type & VM_PROT_WRITE)
				fault_flags = VM_FAULT_DIRTY;
			else
				fault_flags = VM_FAULT_NORMAL;

			rv = vm_fault(&curthread->td_lwp->lwp_vmspace->vm_map,
			    trunc_page(vti->guest_physical_address), fault_type, fault_flags);

			if (rv != KERN_SUCCESS) {
				kprintf("VMM: handle_vmx_vmexit: EXIT_REASON_EPT_FAULT couldn't resolve %llx\n",
				    (unsigned long long) vti->guest_physical_address);
				err = -1;
				goto error;
			}
			break;
		default:
			kprintf("VMM: handle_vmx_vmexit: unknown exit reason: %d with qualification %lld\n",
			    exit_reason, (long long) vti->vmexit_qualification);
			err = -1;
			goto error;
	}
	return 0;
error:
	return err;
}

static int
vmx_vmrun(void)
{
	struct vmx_thread_info * vti;
	struct globaldata *gd;
	int err;
	int ret;
	int sticks = 0;
	uint64_t val;
	cpulock_t olock;
	cpulock_t nlock;
	struct trapframe *save_frame;
	thread_t td = curthread;

	vti = (struct vmx_thread_info *) td->td_vmm;
	save_frame = td->td_lwp->lwp_md.md_regs;
	td->td_lwp->lwp_md.md_regs = &vti->guest;
restart:
	crit_enter();

	/*
	 * This can change the cpu we are running on.
	 */
	trap_handle_userexit(&vti->guest, sticks);
	gd = mycpu;

	ERROR2_IF(vti == NULL);
	ERROR2_IF(vmx_check_cpu_migration());
	ERROR2_IF(vmx_handle_cpu_migration());

	/*
	 * Make the state safe to VMENTER
	 * - disable interrupts and check if there were any pending
	 * - check for ASTFLTs
	 * - loop again until there are no ASTFLTs
	 */
	cpu_disable_intr();
	splz();
	if (gd->gd_reqflags & RQF_AST_MASK) {
		atomic_clear_int(&gd->gd_reqflags, RQF_AST_SIGNAL);
		cpu_enable_intr();
		crit_exit();
		vti->guest.tf_trapno = T_ASTFLT;
		trap(&vti->guest);
		/* CURRENT CPU CAN CHANGE */
		goto restart;
	}
	if (vti->last_cpu != gd->gd_cpuid) {
		cpu_enable_intr();
		crit_exit();
		kprintf("VMM: vmx_vmrun: vti unexpectedly "
			"changed cpus %d->%d\n",
			gd->gd_cpuid, vti->last_cpu);
		goto restart;
	}

	/*
	 * Add us to the list of cpus running vkernel operations, interlock
	 * against anyone trying to do an invalidation.
	 *
	 * We must set the cpumask first to ensure that we interlock another
	 * cpu that may desire to IPI us after we have successfully
	 * incremented the cpulock counter.
	 */
	atomic_set_cpumask(&td->td_proc->p_vmm_cpumask, gd->gd_cpumask);

        for (;;) {
		olock = td->td_proc->p_vmm_cpulock;
		cpu_ccfence();
		if ((olock & CPULOCK_EXCL) == 0) {
			nlock = olock + CPULOCK_INCR;
			if (atomic_cmpset_int(&td->td_proc->p_vmm_cpulock,
					      olock, nlock)) {
				/* fast path */
				break;
			}
			/* cmpset race */
			cpu_pause();
			continue;
		}

		/*
		 * More complex.  After sleeping we have to re-test
		 * everything.
		 */
		atomic_clear_cpumask(&td->td_proc->p_vmm_cpumask,
				     gd->gd_cpumask);
		cpu_enable_intr();
		tsleep_interlock(&td->td_proc->p_vmm_cpulock, 0);
		if (td->td_proc->p_vmm_cpulock & CPULOCK_EXCL) {
			tsleep(&td->td_proc->p_vmm_cpulock, PINTERLOCKED,
			       "vmminvl", hz);
		}
		crit_exit();
		goto restart;
	}

	/*
	 * Load specific Guest registers
	 * GP registers will be loaded in vmx_launch/resume
	 */
	ERROR_IF(vmwrite(VMCS_GUEST_RIP, vti->guest.tf_rip));
	ERROR_IF(vmwrite(VMCS_GUEST_CS_SELECTOR, vti->guest.tf_cs));
	ERROR_IF(vmwrite(VMCS_GUEST_RFLAGS, vti->guest.tf_rflags));
	ERROR_IF(vmwrite(VMCS_GUEST_RSP, vti->guest.tf_rsp));
	ERROR_IF(vmwrite(VMCS_GUEST_SS_SELECTOR, vti->guest.tf_ss));
	ERROR_IF(vmwrite(VMCS_GUEST_CR3, (uint64_t) vti->guest_cr3));

	/*
	 * FPU
	 */
	if (mdcpu->gd_npxthread != td) {
		if (mdcpu->gd_npxthread)
			npxsave(mdcpu->gd_npxthread->td_savefpu);
		npxdna();
	}

	/*
	 * The kernel caches the MSR_FSBASE value in mdcpu->gd_user_fs.
	 * A vmexit loads this unconditionally from the VMCS so make
	 * sure it loads the correct value.
	 */
	ERROR_IF(vmwrite(VMCS_HOST_FS_BASE, mdcpu->gd_user_fs));

	/*
	 * EPT mappings can't be invalidated with normal invlpg/invltlb
	 * instructions. We have to execute a special instruction that
	 * invalidates all EPT cache ("invept").
	 *
	 * pm_invgen it's a generation number which is incremented in the
	 * pmap_inval_interlock, before doing any invalidates. The
	 * pmap_inval_interlock will cause all the CPUs that are using
	 * the EPT to VMEXIT and wait for the interlock to complete.
	 * When they will VMENTER they will see that the generation
	 * number had changed from their current and do a invept.
	 */
	if (vti->eptgen != td->td_proc->p_vmspace->vm_pmap.pm_invgen) {
		vti->eptgen = td->td_proc->p_vmspace->vm_pmap.pm_invgen;

		ERROR_IF(invept(INVEPT_TYPE_SINGLE_CONTEXT,
		    (uint64_t*)&vti->invept_desc));
	}

	if (vti->launched) { /* vmresume called from vmx_trap.s */
		dkprintf("\n\nVMM: vmx_vmrun: vmx_resume\n");
		ret = vmx_resume(vti);

	} else { /* vmlaunch called from vmx_trap.s */
		dkprintf("\n\nVMM: vmx_vmrun: vmx_launch\n");
		vti->launched = 1;
		ret = vmx_launch(vti);
	}

	/*
	 * This is our return point from the vmlaunch/vmresume
	 * There are two situations:
	 * - the vmlaunch/vmresume executed successfully and they
	 *   would return through "vmx_vmexit" which will restore
	 *   the state (registers) and return here with the ret
	 *   set to VM_EXIT (ret is actually %rax)
	 * - the vmlaunch/vmresume failed to execute and will return
	 *   immediately with ret set to the error code
	 */
	if (ret == VM_EXIT) {
		ERROR_IF(vmx_vmexit_loadinfo());

		atomic_clear_cpumask(&td->td_proc->p_vmm_cpumask,
				     gd->gd_cpumask);
		atomic_add_int(&td->td_proc->p_vmm_cpulock,
			       -CPULOCK_INCR);
		/* WARNING: don't adjust cpulock twice! */

		cpu_enable_intr();
		trap_handle_userenter(td);
		sticks = td->td_sticks;
		crit_exit();

		/*
		 * Handle the VMEXIT reason
		 * - if successful we VMENTER again
		 * - if not, we exit
		 */
		if (vmx_handle_vmexit())
			goto done;

		/*
		 * We handled the VMEXIT reason and continue with
		 * VM execution
		 */
		goto restart;

	} else {
		vti->launched = 0;

		/*
		 * Two types of error:
		 * - VM_FAIL_VALID - the host state was ok,
		 *   but probably the guest state was not
		 * - VM_FAIL_INVALID - the parameters or the host state
		 *   was not ok
		 */
		if (ret == VM_FAIL_VALID) {
			vmread(VMCS_INSTR_ERR, &val);
			err = (int) val;
			kprintf("VMM: vmx_vmrun: vmenter failed with "
				"VM_FAIL_VALID, error code %d\n",
				err);
		} else {
			kprintf("VMM: vmx_vmrun: vmenter failed with "
				"VM_FAIL_INVALID\n");
		}
		goto error;
	}
done:
	kprintf("VMM: vmx_vmrun: returning with success\n");
	return 0;
error:
	atomic_clear_cpumask(&td->td_proc->p_vmm_cpumask, gd->gd_cpumask);
	atomic_add_int(&td->td_proc->p_vmm_cpulock, -CPULOCK_INCR);
	cpu_enable_intr();
error2:
	trap_handle_userenter(td);
	td->td_lwp->lwp_md.md_regs = save_frame;
	KKASSERT((td->td_proc->p_vmm_cpumask & gd->gd_cpumask) == 0);
	/*atomic_clear_cpumask(&td->td_proc->p_vmm_cpumask, gd->gd_cpumask);*/
	crit_exit();
	kprintf("VMM: vmx_vmrun failed\n");
	return err;
}

/*
 * Called when returning to user-space
 * after executing lwp_fork.
 */
static void
vmx_lwp_return(struct lwp *lp, struct trapframe *frame)
{
	struct vmm_guest_options options;
	int vmrun_err;
	struct vmm_proc *p_vmm = (struct vmm_proc *)curproc->p_vmm;

	dkprintf("VMM: vmx_lwp_return \n");

	bzero(&options, sizeof(struct vmm_guest_options));

	bcopy(frame, &options.tf, sizeof(struct trapframe));

	options.guest_cr3 = p_vmm->guest_cr3;
	options.vmm_cr3 = p_vmm->vmm_cr3;

	vmx_vminit(&options);
	generic_lwp_return(lp, frame);

	vmrun_err = vmx_vmrun();

	exit1(W_EXITCODE(vmrun_err, 0));
}

static void
vmx_set_guest_cr3(register_t guest_cr3)
{
	struct vmx_thread_info *vti = (struct vmx_thread_info *) curthread->td_vmm;
	vti->guest_cr3 = guest_cr3;
}

static int
vmx_vm_get_gpa(struct proc *p, register_t *gpa, register_t uaddr)
{
	return guest_phys_addr(p->p_vmspace, gpa, p->p_vkernel->vkernel_cr3, uaddr);
}

static struct vmm_ctl ctl_vmx = {
	.name = "VMX from Intel",
	.init = vmx_init,
	.enable = vmx_enable,
	.disable = vmx_disable,
	.vminit = vmx_vminit,
	.vmdestroy = vmx_vmdestroy,
	.vmrun = vmx_vmrun,
	.vm_set_tls_area = vmx_set_tls_area,
	.vm_lwp_return = vmx_lwp_return,
	.vm_set_guest_cr3 = vmx_set_guest_cr3,
	.vm_get_gpa = vmx_vm_get_gpa,
};

struct vmm_ctl*
get_ctl_intel(void)
{
	return &ctl_vmx;
}
