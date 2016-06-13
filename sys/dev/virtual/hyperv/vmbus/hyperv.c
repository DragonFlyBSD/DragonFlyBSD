/*-
 * Copyright (c) 2009-2012,2016 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systimer.h>
#include <sys/systm.h>

#include <machine/cpufunc.h>

#include <dev/virtual/hyperv/include/hyperv_busdma.h>
#include <dev/virtual/hyperv/vmbus/hyperv_machdep.h>
#include <dev/virtual/hyperv/vmbus/hyperv_reg.h>
#include <dev/virtual/hyperv/vmbus/hyperv_var.h>

#define HYPERV_DRAGONFLY_BUILD		0ULL
#define HYPERV_DRAGONFLY_VERSION	((uint64_t)__DragonFly_version)
#define HYPERV_DRAGONFLY_OSID		0ULL

#define MSR_HV_GUESTID_BUILD_DRAGONFLY	\
	(HYPERV_DRAGONFLY_BUILD & MSR_HV_GUESTID_BUILD_MASK)
#define MSR_HV_GUESTID_VERSION_DRAGONFLY \
	((HYPERV_DRAGONFLY_VERSION << MSR_HV_GUESTID_VERSION_SHIFT) & \
	 MSR_HV_GUESTID_VERSION_MASK)
#define MSR_HV_GUESTID_OSID_DRAGONFLY	\
	((HYPERV_DRAGONFLY_OSID << MSR_HV_GUESTID_OSID_SHIFT) & \
	 MSR_HV_GUESTID_OSID_MASK)

#define MSR_HV_GUESTID_DRAGONFLY	\
	(MSR_HV_GUESTID_BUILD_DRAGONFLY | \
	 MSR_HV_GUESTID_VERSION_DRAGONFLY | \
	 MSR_HV_GUESTID_OSID_DRAGONFLY | \
	 MSR_HV_GUESTID_OSTYPE_FREEBSD)

struct hypercall_ctx {
	void			*hc_addr;
	struct hyperv_dma	hc_dma;
};

static void		hyperv_cputimer_construct(struct cputimer *,
			    sysclock_t);
static sysclock_t	hyperv_cputimer_count(void);
static boolean_t	hyperv_identify(void);
static int		hypercall_create(void);
static void		hypercall_destroy(void);
static void		hypercall_memfree(void);

u_int			hyperv_features;
static u_int		hyperv_recommends;

static u_int		hyperv_pm_features;
static u_int		hyperv_features3;

static struct cputimer	hyperv_cputimer = {
	SLIST_ENTRY_INITIALIZER,
	"Hyper-V",
	CPUTIMER_PRI_VMM,
	CPUTIMER_VMM,
	hyperv_cputimer_count,
	cputimer_default_fromhz,
	cputimer_default_fromus,
	hyperv_cputimer_construct,
	cputimer_default_destruct,
	HYPERV_TIMER_FREQ,
	0, 0, 0
};

static struct hypercall_ctx	hypercall_context;

uint64_t
hypercall_post_message(bus_addr_t msg_paddr)
{
	return hypercall_md(hypercall_context.hc_addr,
	    HYPERCALL_POST_MESSAGE, msg_paddr, 0);
}

static void
hyperv_cputimer_construct(struct cputimer *timer, sysclock_t oldclock)
{
	timer->base = 0;
	timer->base = oldclock - timer->count();
}

static sysclock_t
hyperv_cputimer_count(void)
{
	uint64_t val;

	val = rdmsr(MSR_HV_TIME_REF_COUNT);
	return (val + hyperv_cputimer.base);
}

static void
hypercall_memfree(void)
{
	hyperv_dmamem_free(&hypercall_context.hc_dma,
	    hypercall_context.hc_addr);
	hypercall_context.hc_addr = NULL;
}

static int
hypercall_create(void)
{
	uint64_t hc, hc_orig;

	hypercall_context.hc_addr = hyperv_dmamem_alloc(NULL, PAGE_SIZE, 0,
	    PAGE_SIZE, &hypercall_context.hc_dma, BUS_DMA_WAITOK);
	if (hypercall_context.hc_addr == NULL) {
		kprintf("hyperv: Hypercall page allocation failed\n");
		return ENOMEM;
	}

	/* Get the 'reserved' bits, which requires preservation. */
	hc_orig = rdmsr(MSR_HV_HYPERCALL);

	/*
	 * Setup the Hypercall page.
	 *
	 * NOTE: 'reserved' bits MUST be preserved.
	 */
	hc = ((hypercall_context.hc_dma.hv_paddr >> PAGE_SHIFT) <<
	    MSR_HV_HYPERCALL_PGSHIFT) |
	    (hc_orig & MSR_HV_HYPERCALL_RSVD_MASK) |
	    MSR_HV_HYPERCALL_ENABLE;
	wrmsr(MSR_HV_HYPERCALL, hc);

	/*
	 * Confirm that Hypercall page did get setup.
	 */
	hc = rdmsr(MSR_HV_HYPERCALL);
	if ((hc & MSR_HV_HYPERCALL_ENABLE) == 0) {
		kprintf("hyperv: Hypercall setup failed\n");
		hypercall_memfree();
		return EIO;
	}
	if (bootverbose)
		kprintf("hyperv: Hypercall created\n");

	return 0;
}

static void
hypercall_destroy(void)
{
	uint64_t hc;

	if (hypercall_context.hc_addr == NULL)
		return;

	/* Disable Hypercall */
	hc = rdmsr(MSR_HV_HYPERCALL);
	wrmsr(MSR_HV_HYPERCALL, (hc & MSR_HV_HYPERCALL_RSVD_MASK));
	hypercall_memfree();

	if (bootverbose)
		kprintf("hyperv: Hypercall destroyed\n");
}

static boolean_t
hyperv_identify(void)
{
	u_int regs[4];
	unsigned int maxleaf;

	if (vmm_guest != VMM_GUEST_HYPERV)
		return (FALSE);

	do_cpuid(CPUID_LEAF_HV_MAXLEAF, regs);
	maxleaf = regs[0];
	if (maxleaf < CPUID_LEAF_HV_LIMITS)
		return (FALSE);

	do_cpuid(CPUID_LEAF_HV_INTERFACE, regs);
	if (regs[0] != CPUID_HV_IFACE_HYPERV)
		return (FALSE);

	do_cpuid(CPUID_LEAF_HV_FEATURES, regs);
	if ((regs[0] & CPUID_HV_MSR_HYPERCALL) == 0) {
		/*
		 * Hyper-V w/o Hypercall is impossible; someone
		 * is faking Hyper-V.
		 */
		return (FALSE);
	}
	hyperv_features = regs[0];
	hyperv_pm_features = regs[2];
	hyperv_features3 = regs[3];

	do_cpuid(CPUID_LEAF_HV_IDENTITY, regs);
	kprintf("Hyper-V Version: %d.%d.%d [SP%d]\n",
	    regs[1] >> 16, regs[1] & 0xffff, regs[0], regs[2]);

	kprintf("  Features=0x%b\n", hyperv_features,
	    "\020"
	    "\001VPRUNTIME"	/* MSR_HV_VP_RUNTIME */
	    "\002TMREFCNT"	/* MSR_HV_TIME_REF_COUNT */
	    "\003SYNIC"		/* MSRs for SynIC */
	    "\004SYNTM"		/* MSRs for SynTimer */
	    "\005APIC"		/* MSR_HV_{EOI,ICR,TPR} */
	    "\006HYPERCALL"	/* MSR_HV_{GUEST_OS_ID,HYPERCALL} */
	    "\007VPINDEX"	/* MSR_HV_VP_INDEX */
	    "\010RESET"		/* MSR_HV_RESET */
	    "\011STATS"		/* MSR_HV_STATS_ */
	    "\012REFTSC"	/* MSR_HV_REFERENCE_TSC */
	    "\013IDLE"		/* MSR_HV_GUEST_IDLE */
	    "\014TMFREQ"	/* MSR_HV_{TSC,APIC}_FREQUENCY */
	    "\015DEBUG");	/* MSR_HV_SYNTH_DEBUG_ */
	kprintf("  PM Features=0x%b [C%u]\n",
	    (hyperv_pm_features & ~CPUPM_HV_CSTATE_MASK),
	    "\020"
	    "\005C3HPET",	/* HPET is required for C3 state */
	    CPUPM_HV_CSTATE(hyperv_pm_features));
	kprintf("  Features3=0x%b\n", hyperv_features3,
	    "\020"
	    "\001MWAIT"		/* MWAIT */
	    "\002DEBUG"		/* guest debug support */
	    "\003PERFMON"	/* performance monitor */
	    "\004PCPUDPE"	/* physical CPU dynamic partition event */
	    "\005XMMHC"		/* hypercall input through XMM regs */
	    "\006IDLE"		/* guest idle support */
	    "\007SLEEP"		/* hypervisor sleep support */
	    "\010NUMA"		/* NUMA distance query support */
	    "\011TMFREQ"	/* timer frequency query (TSC, LAPIC) */
	    "\012SYNCMC"	/* inject synthetic machine checks */
	    "\013CRASH"		/* MSRs for guest crash */
	    "\014DEBUGMSR"	/* MSRs for guest debug */
	    "\015NPIEP"		/* NPIEP */
	    "\016HVDIS");	/* disabling hypervisor */

	do_cpuid(CPUID_LEAF_HV_RECOMMENDS, regs);
	hyperv_recommends = regs[0];
	if (bootverbose)
		kprintf("  Recommends: %08x %08x\n", regs[0], regs[1]);

	do_cpuid(CPUID_LEAF_HV_LIMITS, regs);
	if (bootverbose) {
		kprintf("  Limits: Vcpu:%d Lcpu:%d Int:%d\n",
		    regs[0], regs[1], regs[2]);
	}

	if (maxleaf >= CPUID_LEAF_HV_HWFEATURES) {
		do_cpuid(CPUID_LEAF_HV_HWFEATURES, regs);
		if (bootverbose) {
			kprintf("  HW Features: %08x, AMD: %08x\n",
			    regs[0], regs[3]);
		}
	}

	return (TRUE);
}

static void
hyperv_init(void *dummy __unused)
{
	int error;

	if (!hyperv_identify()) {
		/* Not Hyper-V; reset guest id to the generic one. */
		if (vmm_guest == VMM_GUEST_HYPERV)
			vmm_guest = VMM_GUEST_UNKNOWN;
		return;
	}

	/* Set guest id */
	wrmsr(MSR_HV_GUEST_OS_ID, MSR_HV_GUESTID_DRAGONFLY);

	if (hyperv_features & CPUID_HV_MSR_TIME_REFCNT) {
		/* Register Hyper-V systimer */
		cputimer_register(&hyperv_cputimer);
		cputimer_select(&hyperv_cputimer, 0);
	}

	error = hypercall_create();
	if (error) {
		/* Can't perform any Hyper-V specific actions */
		vmm_guest = VMM_GUEST_UNKNOWN;
	}
}
SYSINIT(hyperv_initialize, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST,
    hyperv_init, NULL);

static void
hyperv_uninit(void *dummy __unused)
{
	if (hyperv_features & CPUID_HV_MSR_TIME_REFCNT) {
		/* Deregister Hyper-V systimer */
		cputimer_deregister(&hyperv_cputimer);
	}
	hypercall_destroy();
}
SYSUNINIT(hyperv_uninitialize, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST,
    hyperv_uninit, NULL);
