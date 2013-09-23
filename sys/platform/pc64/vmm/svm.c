#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/thread.h>
#include <sys/vmm.h>

#include <vm/pmap.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#include "vmm.h"
#include "svm.h"

static int svm_available = 0;
static int svm_enabled = 0;

//static int svm_rvi_available = 0;
//static int svm_vmcb_caching_available = 0;

static vm_offset_t vm_hsave_va[MAXCPU];

/*
 * svm_init() - Identify AMD SVM support.
 *
 *	Called in early boot. Detects AMD SVM support and extended features.
 */
static int svm_init(void) {
	uint64_t vm_cr;

	/* Not fully implemented and will break vkernel64 bootstrap */
	return (ENODEV);

	/* SVM is identified by CPUID */
	if ((amd_feature2 & AMDID2_SVM) == 0)
		return (ENODEV);

	/* Processor may support SVM, but it may be disabled. */
	vm_cr = rdmsr(MSR_AMD_VM_CR);
	if (vm_cr & MSR_AMD_VM_CR_SVMDIS)
		return (ENODEV);

	svm_available = 1;

	return (0);
}

/*
 * svm_enable() - Called to enable SVM extensions on every processor.
 */
static int svm_enable(void) {
	uint64_t efer;
	int origcpu;
	int i;
	vm_paddr_t vm_hsave_pa;

	if (!svm_available)
		return (ENODEV);

	KKASSERT(svm_enabled == 0);

	/* Set EFER.SVME and allocate a VM Host Save Area on every cpu */
	origcpu = mycpuid;
	for (i = 0; i < ncpus; i++) {
		lwkt_migratecpu(i);

		efer = rdmsr(MSR_EFER);
		efer |= EFER_SVME;
		wrmsr(MSR_EFER, efer);

		vm_hsave_va[i] = (vm_offset_t) contigmalloc(4096, M_TEMP,
							    M_WAITOK | M_ZERO,
							    0, 0xffffffff,
							    4096, 0);
		vm_hsave_pa = vtophys(vm_hsave_va[i]);
		wrmsr(MSR_AMD_VM_HSAVE_PA, vm_hsave_pa);
	}
	lwkt_migratecpu(origcpu);

	svm_enabled = 1;

	return (0);
}

/*
 * svm_disable() - Called to disable SVM extensions on every processor.
 */
static int svm_disable(void) {
	uint64_t efer;
	int origcpu;
	int i;

	/* XXX Wait till no vmms are running? */

	KKASSERT(svm_enabled == 1);

	origcpu = mycpuid;
	for (i = 0; i < ncpus; i++) {
		lwkt_migratecpu(i);

		wrmsr(MSR_AMD_VM_HSAVE_PA, 0);

		contigfree((void *) vm_hsave_va[i], 4096, M_TEMP);
		vm_hsave_va[i] = 0;

		efer = rdmsr(MSR_EFER);
		efer &= ~EFER_SVME;
		wrmsr(MSR_EFER, efer);
	}
	lwkt_migratecpu(origcpu);

	svm_enabled = 0;

	return (0);
}

/*
 * svm_vminit() - Prepare current thread for VMRUN.
 *
 *	Allocates a VMCB for the current thread and flags the thread to return
 *	to usermode via svm_vmrun().
 */
static int svm_vminit(struct guest_options *options) {
	return (ENODEV);
}

/*
 * svm_vmdestroy() -
 */
static int svm_vmdestroy(void) {
	return (ENODEV);
}

/*
 * svm_vmrun() - Execute VMRUN on a prepared VMCB for a thread.
 *
 *	Called while a thread is returning to userspace, after being flagged as
 *	a VMM thread. svm_vmrun() continues in a loop around VMRUN/#VMEXIT
 *	handling until we are no longer a VMM thread.
 */
static int svm_vmrun(void) {
	return (ENODEV);
}

static struct vmm_ctl ctl_svm = {
	.name = "SVM",
	.init = svm_init,
	.enable = svm_enable,
	.disable = svm_disable,
	.vminit = svm_vminit,
	.vmdestroy = svm_vmdestroy,
	.vmrun = svm_vmrun,
};

struct vmm_ctl *
get_ctl_amd(void)
{
	return &ctl_svm;
}
