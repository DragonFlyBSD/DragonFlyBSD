/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 */


#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/memrange.h>
#include <sys/tls.h>
#include <sys/types.h>

#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>

#include <sys/mplock2.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/globaldata.h>
#include <machine/md_var.h>
#include <machine/pmap.h>
#include <machine/smp.h>
#include <machine/tls.h>

#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>

extern pt_entry_t *KPTphys;

volatile cpumask_t stopped_cpus;
cpumask_t	smp_active_mask = 1;  /* which cpus are ready for IPIs etc? */
static int	boot_address;
static cpumask_t smp_startup_mask = 1;  /* which cpus have been started */
int		mp_naps;                /* # of Applications processors */
static int  mp_finish;

/* Local data for detecting CPU TOPOLOGY */
static int core_bits = 0;
static int logical_CPU_bits = 0;

/* function prototypes XXX these should go elsewhere */
void bootstrap_idle(void);
void single_cpu_ipi(int, int, int);
void selected_cpu_ipi(cpumask_t, int, int);
#if 0
void ipi_handler(int);
#endif

pt_entry_t *SMPpt;

/* AP uses this during bootstrap.  Do not staticize.  */
char *bootSTK;
static int bootAP;


/* XXX these need to go into the appropriate header file */
static int start_all_aps(u_int);
void init_secondary(void);
void *start_ap(void *);

/*
 * Get SMP fully working before we start initializing devices.
 */
static
void
ap_finish(void)
{
	int i;
	cpumask_t ncpus_mask = 0;

	for (i = 1; i <= ncpus; i++)
		ncpus_mask |= CPUMASK(i);

        mp_finish = 1;
        if (bootverbose)
                kprintf("Finish MP startup\n");

	/* build our map of 'other' CPUs */
	mycpu->gd_other_cpus = smp_startup_mask & ~CPUMASK(mycpu->gd_cpuid);

	/*
	 * Let the other cpu's finish initializing and build their map
	 * of 'other' CPUs.
	 */
        rel_mplock();
        while (smp_active_mask != smp_startup_mask) {
		DELAY(100000);
                cpu_lfence();
	}

        while (try_mplock() == 0)
		DELAY(100000);
        if (bootverbose)
                kprintf("Active CPU Mask: %08lx\n", (long)smp_active_mask);
}

SYSINIT(finishsmp, SI_BOOT2_FINISH_SMP, SI_ORDER_FIRST, ap_finish, NULL)


void *
start_ap(void *arg __unused)
{
	init_secondary();
	setrealcpu();
	bootstrap_idle();

	return(NULL); /* NOTREACHED */
}

/* storage for AP thread IDs */
pthread_t ap_tids[MAXCPU];

void
mp_start(void)
{
	int shift;

	ncpus = optcpus;

	mp_naps = ncpus - 1;

	/* ncpus2 -- ncpus rounded down to the nearest power of 2 */
	for (shift = 0; (1 << shift) <= ncpus; ++shift)
		;
	--shift;
	ncpus2_shift = shift;
	ncpus2 = 1 << shift;
	ncpus2_mask = ncpus2 - 1;

        /* ncpus_fit -- ncpus rounded up to the nearest power of 2 */
        if ((1 << shift) < ncpus)
                ++shift;
        ncpus_fit = 1 << shift;
        ncpus_fit_mask = ncpus_fit - 1;

	/*
	 * cpu0 initialization
	 */
	mycpu->gd_ipiq = (void *)kmem_alloc(&kernel_map,
					    sizeof(lwkt_ipiq) * ncpus);
	bzero(mycpu->gd_ipiq, sizeof(lwkt_ipiq) * ncpus);

	/*
	 * cpu 1-(n-1)
	 */
	start_all_aps(boot_address);

}

void
mp_announce(void)
{
	int x;

	kprintf("DragonFly/MP: Multiprocessor\n");
	kprintf(" cpu0 (BSP)\n");

	for (x = 1; x <= mp_naps; ++x)
		kprintf(" cpu%d (AP)\n", x);
}

void
cpu_send_ipiq(int dcpu)
{
	if (CPUMASK(dcpu) & smp_active_mask) {
		if (pthread_kill(ap_tids[dcpu], SIGUSR1) != 0)
			panic("pthread_kill failed in cpu_send_ipiq");
	}
#if 0
	panic("XXX cpu_send_ipiq()");
#endif
}

void
smp_invltlb(void)
{
#ifdef SMP
#endif
}

void
single_cpu_ipi(int cpu, int vector, int delivery_mode)
{
	kprintf("XXX single_cpu_ipi\n");
}

void
selected_cpu_ipi(cpumask_t target, int vector, int delivery_mode)
{
	crit_enter();
	while (target) {
		int n = BSFCPUMASK(target);
		target &= ~CPUMASK(n);
		single_cpu_ipi(n, vector, delivery_mode);
	}
	crit_exit();
}

int
stop_cpus(cpumask_t map)
{
	map &= smp_active_mask;

	crit_enter();
	while (map) {
		int n = BSFCPUMASK(map);
		map &= ~CPUMASK(n);
		stopped_cpus |= CPUMASK(n);
		if (pthread_kill(ap_tids[n], SIGXCPU) != 0)
			panic("stop_cpus: pthread_kill failed");
	}
	crit_exit();
#if 0
	panic("XXX stop_cpus()");
#endif

	return(1);
}

int
restart_cpus(cpumask_t map)
{
	map &= smp_active_mask;

	crit_enter();
	while (map) {
		int n = BSFCPUMASK(map);
		map &= ~CPUMASK(n);
		stopped_cpus &= ~CPUMASK(n);
		if (pthread_kill(ap_tids[n], SIGXCPU) != 0)
			panic("restart_cpus: pthread_kill failed");
	}
	crit_exit();
#if 0
	panic("XXX restart_cpus()");
#endif

	return(1);
}

void
ap_init(void)
{
        /*
         * Adjust smp_startup_mask to signal the BSP that we have started
         * up successfully.  Note that we do not yet hold the BGL.  The BSP
         * is waiting for our signal.
         *
         * We can't set our bit in smp_active_mask yet because we are holding
         * interrupts physically disabled and remote cpus could deadlock
         * trying to send us an IPI.
         */
	smp_startup_mask |= CPUMASK(mycpu->gd_cpuid);
	cpu_mfence();

        /*
         * Interlock for finalization.  Wait until mp_finish is non-zero,
         * then get the MP lock.
         *
         * Note: We are in a critical section.
         *
         * Note: we are the idle thread, we can only spin.
         *
         * Note: The load fence is memory volatile and prevents the compiler
         * from improperly caching mp_finish, and the cpu from improperly
         * caching it.
         */

	while (mp_finish == 0) {
		cpu_lfence();
		DELAY(500000);
	}
        while (try_mplock() == 0)
		DELAY(100000);

        /* BSP may have changed PTD while we're waiting for the lock */
        cpu_invltlb();

        /* Build our map of 'other' CPUs. */
        mycpu->gd_other_cpus = smp_startup_mask & ~CPUMASK(mycpu->gd_cpuid);

        kprintf("SMP: AP CPU #%d Launched!\n", mycpu->gd_cpuid);


        /* Set memory range attributes for this CPU to match the BSP */
        mem_range_AP_init();
        /*
         * Once we go active we must process any IPIQ messages that may
         * have been queued, because no actual IPI will occur until we
         * set our bit in the smp_active_mask.  If we don't the IPI
         * message interlock could be left set which would also prevent
         * further IPIs.
         *
         * The idle loop doesn't expect the BGL to be held and while
         * lwkt_switch() normally cleans things up this is a special case
         * because we returning almost directly into the idle loop.
         *
         * The idle thread is never placed on the runq, make sure
         * nothing we've done put it there.
         */
	KKASSERT(get_mplock_count(curthread) == 1);
        smp_active_mask |= CPUMASK(mycpu->gd_cpuid);

	mdcpu->gd_fpending = 0;
	mdcpu->gd_ipending = 0;
	initclocks_pcpu();	/* clock interrupts (via IPIs) */
	lwkt_process_ipiq();

        /*
         * Releasing the mp lock lets the BSP finish up the SMP init
         */
        rel_mplock();
        KKASSERT((curthread->td_flags & TDF_RUNQ) == 0);
}

void
init_secondary(void)
{
        int     myid = bootAP;
        struct mdglobaldata *md;
        struct privatespace *ps;

        ps = &CPU_prvspace[myid];

	KKASSERT(ps->mdglobaldata.mi.gd_prvspace == ps);

	/*
	 * Setup the %gs for cpu #n.  The mycpu macro works after this
	 * point.  Note that %fs is used by pthreads.
	 */
	tls_set_gs(&CPU_prvspace[myid], sizeof(struct privatespace));

        md = mdcpu;     /* loaded through %gs:0 (mdglobaldata.mi.gd_prvspace)*/

	/* JG */
        md->gd_common_tss.tss_rsp0 = 0; /* not used until after switch */
        //md->gd_common_tss.tss_ss0 = GSEL(GDATA_SEL, SEL_KPL);
        //md->gd_common_tss.tss_ioopt = (sizeof md->gd_common_tss) << 16;

        /*
         * Set to a known state:
         * Set by mpboot.s: CR0_PG, CR0_PE
         * Set by cpu_setregs: CR0_NE, CR0_MP, CR0_TS, CR0_WP, CR0_AM
         */
}

static int
start_all_aps(u_int boot_addr)
{
	int x, i;
	struct mdglobaldata *gd;
	struct privatespace *ps;
	vm_page_t m;
	vm_offset_t va;
#if 0
	struct lwp_params params;
#endif

	/*
	 * needed for ipis to initial thread
	 * FIXME: rename ap_tids?
	 */
	ap_tids[0] = pthread_self();

	vm_object_hold(&kernel_object);
	for (x = 1; x <= mp_naps; x++)
	{
		/* Allocate space for the CPU's private space. */
		for (i = 0; i < sizeof(struct mdglobaldata); i += PAGE_SIZE) {
			va =(vm_offset_t)&CPU_prvspace[x].mdglobaldata + i;
			m = vm_page_alloc(&kernel_object, va, VM_ALLOC_SYSTEM);
			pmap_kenter_quick(va, m->phys_addr);
		}

		for (i = 0; i < sizeof(CPU_prvspace[x].idlestack); i += PAGE_SIZE) {
			va =(vm_offset_t)&CPU_prvspace[x].idlestack + i;
			m = vm_page_alloc(&kernel_object, va, VM_ALLOC_SYSTEM);
			pmap_kenter_quick(va, m->phys_addr);
		}

                gd = &CPU_prvspace[x].mdglobaldata;     /* official location */
                bzero(gd, sizeof(*gd));
                gd->mi.gd_prvspace = ps = &CPU_prvspace[x];

                /* prime data page for it to use */
                mi_gdinit(&gd->mi, x);
                cpu_gdinit(gd, x);

#if 0
                gd->gd_CMAP1 = pmap_kpte((vm_offset_t)CPU_prvspace[x].CPAGE1);
                gd->gd_CMAP2 = pmap_kpte((vm_offset_t)CPU_prvspace[x].CPAGE2);
                gd->gd_CMAP3 = pmap_kpte((vm_offset_t)CPU_prvspace[x].CPAGE3);
                gd->gd_PMAP1 = pmap_kpte((vm_offset_t)CPU_prvspace[x].PPAGE1);
                gd->gd_CADDR1 = ps->CPAGE1;
                gd->gd_CADDR2 = ps->CPAGE2;
                gd->gd_CADDR3 = ps->CPAGE3;
                gd->gd_PADDR1 = (vpte_t *)ps->PPAGE1;
#endif

                gd->mi.gd_ipiq = (void *)kmem_alloc(&kernel_map, sizeof(lwkt_ipiq) * (mp_naps + 1));
                bzero(gd->mi.gd_ipiq, sizeof(lwkt_ipiq) * (mp_naps + 1));

                /*
                 * Setup the AP boot stack
                 */
                bootSTK = &ps->idlestack[UPAGES*PAGE_SIZE/2];
                bootAP = x;

		/*
		 * Setup the AP's lwp, this is the 'cpu'
		 *
		 * We have to make sure our signals are masked or the new LWP
		 * may pick up a signal that it isn't ready for yet.  SMP
		 * startup occurs after SI_BOOT2_LEAVE_CRIT so interrupts
		 * have already been enabled.
		 */
		cpu_disable_intr();
		pthread_create(&ap_tids[x], NULL, start_ap, NULL);
		cpu_enable_intr();

		while((smp_startup_mask & CPUMASK(x)) == 0) {
			cpu_lfence(); /* XXX spin until the AP has started */
			DELAY(1000);
		}
	}
	vm_object_drop(&kernel_object);

	return(ncpus - 1);
}

/*
 * CPU TOPOLOGY DETECTION FUNCTIONS.
 */

void
detect_cpu_topology(void)
{
	logical_CPU_bits = vkernel_b_arg;
	core_bits = vkernel_B_arg;
}

int
get_chip_ID(int cpuid)
{
	return get_apicid_from_cpuid(cpuid) >>
	    (logical_CPU_bits + core_bits);
}

int
get_core_number_within_chip(int cpuid)
{
	return (get_apicid_from_cpuid(cpuid) >> logical_CPU_bits) &
	    ( (1 << core_bits) -1);
}

int
get_logical_CPU_number_within_core(int cpuid)
{
	return get_apicid_from_cpuid(cpuid) &
	    ( (1 << logical_CPU_bits) -1);
}

