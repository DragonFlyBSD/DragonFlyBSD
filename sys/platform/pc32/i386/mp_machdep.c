/*
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/i386/mp_machdep.c,v 1.115.2.15 2003/03/14 21:22:35 jhb Exp $
 */

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/cons.h>	/* cngetc() */
#include <sys/machintr.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <sys/lock.h>
#include <vm/vm_map.h>
#include <sys/user.h>
#ifdef GPROF 
#include <sys/gmon.h>
#endif

#include <sys/mplock2.h>

#include <machine/smp.h>
#include <machine_base/apic/apicreg.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine_base/icu/icu_var.h>
#include <machine_base/apic/ioapic_abi.h>
#include <machine_base/apic/lapic.h>
#include <machine_base/apic/ioapic.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/tss.h>
#include <machine/specialreg.h>
#include <machine/globaldata.h>
#include <machine/pmap_inval.h>

#include <machine/md_var.h>		/* setidt() */
#include <machine_base/icu/icu.h>	/* IPIs */
#include <machine/intr_machdep.h>	/* IPIs */

#define WARMBOOT_TARGET		0
#define WARMBOOT_OFF		(KERNBASE + 0x0467)
#define WARMBOOT_SEG		(KERNBASE + 0x0469)

#define CMOS_REG		(0x70)
#define CMOS_DATA		(0x71)
#define BIOS_RESET		(0x0f)
#define BIOS_WARM		(0x0a)

/*
 * this code MUST be enabled here and in mpboot.s.
 * it follows the very early stages of AP boot by placing values in CMOS ram.
 * it NORMALLY will never be needed and thus the primitive method for enabling.
 *
 */
#if defined(CHECK_POINTS)
#define CHECK_READ(A)	 (outb(CMOS_REG, (A)), inb(CMOS_DATA))
#define CHECK_WRITE(A,D) (outb(CMOS_REG, (A)), outb(CMOS_DATA, (D)))

#define CHECK_INIT(D);				\
	CHECK_WRITE(0x34, (D));			\
	CHECK_WRITE(0x35, (D));			\
	CHECK_WRITE(0x36, (D));			\
	CHECK_WRITE(0x37, (D));			\
	CHECK_WRITE(0x38, (D));			\
	CHECK_WRITE(0x39, (D));

#define CHECK_PRINT(S);				\
	kprintf("%s: %d, %d, %d, %d, %d, %d\n",	\
	   (S),					\
	   CHECK_READ(0x34),			\
	   CHECK_READ(0x35),			\
	   CHECK_READ(0x36),			\
	   CHECK_READ(0x37),			\
	   CHECK_READ(0x38),			\
	   CHECK_READ(0x39));

#else				/* CHECK_POINTS */

#define CHECK_INIT(D)
#define CHECK_PRINT(S)

#endif				/* CHECK_POINTS */

/*
 * Values to send to the POST hardware.
 */
#define MP_BOOTADDRESS_POST	0x10
#define MP_PROBE_POST		0x11
#define MPTABLE_PASS1_POST	0x12

#define MP_START_POST		0x13
#define MP_ENABLE_POST		0x14
#define MPTABLE_PASS2_POST	0x15

#define START_ALL_APS_POST	0x16
#define INSTALL_AP_TRAMP_POST	0x17
#define START_AP_POST		0x18

#define MP_ANNOUNCE_POST	0x19

/** XXX FIXME: where does this really belong, isa.h/isa.c perhaps? */
int	current_postcode;

/** XXX FIXME: what system files declare these??? */
extern struct region_descriptor r_gdt, r_idt;

extern int nkpt;
extern int naps;

int64_t tsc0_offset;
extern int64_t tsc_offsets[];

/* AP uses this during bootstrap.  Do not staticize.  */
char *bootSTK;
static int bootAP;

/* Hotwire a 0->4MB V==P mapping */
extern pt_entry_t *KPTphys;

/*
 * SMP page table page.  Setup by locore to point to a page table
 * page from which we allocate per-cpu privatespace areas io_apics,
 * and so forth.
 */
extern pt_entry_t *SMPpt;

struct pcb stoppcbs[MAXCPU];

/*
 * Local data and functions.
 */

static u_int	boot_address;
static int	mp_finish;
static int	mp_finish_lapic;

static int	start_all_aps(u_int boot_addr);
static void	install_ap_tramp(u_int boot_addr);
static int	start_ap(struct mdglobaldata *gd, u_int boot_addr, int smibest);
static int	smitest(void);
static void	mp_bsp_simple_setup(void);

static cpumask_t smp_startup_mask = 1;	/* which cpus have been started */
static cpumask_t smp_lapic_mask = 1;	/* which cpus have lapic been inited */
cpumask_t smp_active_mask = 1;	/* which cpus are ready for IPIs etc? */
SYSCTL_INT(_machdep, OID_AUTO, smp_active, CTLFLAG_RD, &smp_active_mask, 0, "");

/* Local data for detecting CPU TOPOLOGY */
static int core_bits = 0;
static int logical_CPU_bits = 0;


/*
 * Calculate usable address in base memory for AP trampoline code.
 */
u_int
mp_bootaddress(u_int basemem)
{
	POSTCODE(MP_BOOTADDRESS_POST);

	boot_address = basemem & ~0xfff;	/* round down to 4k boundary */
	if ((basemem - boot_address) < bootMP_size)
		boot_address -= 4096;	/* not enough, lower by 4k */

	return boot_address;
}

/*
 * Print various information about the SMP system hardware and setup.
 */
void
mp_announce(void)
{
	int     x;

	POSTCODE(MP_ANNOUNCE_POST);

	kprintf("DragonFly/MP: Multiprocessor motherboard\n");
	kprintf(" cpu0 (BSP): apic id: %2d\n", CPUID_TO_APICID(0));
	for (x = 1; x <= naps; ++x)
		kprintf(" cpu%d (AP):  apic id: %2d\n", x, CPUID_TO_APICID(x));

	if (!ioapic_enable)
		kprintf(" Warning: APIC I/O disabled\n");
}

/*
 * AP cpu's call this to sync up protected mode.
 *
 * WARNING!  We must ensure that the cpu is sufficiently initialized to
 * be able to use to the FP for our optimized bzero/bcopy code before
 * we enter more mainstream C code.
 *
 * WARNING! %fs is not set up on entry.  This routine sets up %fs.
 */
void
init_secondary(void)
{
	int	gsel_tss;
	int	x, myid = bootAP;
	u_int	cr0;
	struct mdglobaldata *md;
	struct privatespace *ps;

	ps = &CPU_prvspace[myid];

	gdt_segs[GPRIV_SEL].ssd_base = (int)ps;
	gdt_segs[GPROC0_SEL].ssd_base =
		(int) &ps->mdglobaldata.gd_common_tss;
	ps->mdglobaldata.mi.gd_prvspace = ps;

	for (x = 0; x < NGDT; x++) {
		ssdtosd(&gdt_segs[x], &gdt[myid * NGDT + x].sd);
	}

	r_gdt.rd_limit = NGDT * sizeof(gdt[0]) - 1;
	r_gdt.rd_base = (int) &gdt[myid * NGDT];
	lgdt(&r_gdt);			/* does magic intra-segment return */

	lidt(&r_idt);

	lldt(_default_ldt);
	mdcpu->gd_currentldt = _default_ldt;

	gsel_tss = GSEL(GPROC0_SEL, SEL_KPL);
	gdt[myid * NGDT + GPROC0_SEL].sd.sd_type = SDT_SYS386TSS;

	md = mdcpu;	/* loaded through %fs:0 (mdglobaldata.mi.gd_prvspace)*/

	md->gd_common_tss.tss_esp0 = 0;	/* not used until after switch */
	md->gd_common_tss.tss_ss0 = GSEL(GDATA_SEL, SEL_KPL);
	md->gd_common_tss.tss_ioopt = (sizeof md->gd_common_tss) << 16;
	md->gd_tss_gdt = &gdt[myid * NGDT + GPROC0_SEL].sd;
	md->gd_common_tssd = *md->gd_tss_gdt;
	ltr(gsel_tss);

	/*
	 * Set to a known state:
	 * Set by mpboot.s: CR0_PG, CR0_PE
	 * Set by cpu_setregs: CR0_NE, CR0_MP, CR0_TS, CR0_WP, CR0_AM
	 */
	cr0 = rcr0();
	cr0 &= ~(CR0_CD | CR0_NW | CR0_EM);
	load_cr0(cr0);
	pmap_set_opt();		/* PSE/4MB pages, etc */

	pmap_init_pat();	/* Page Attribute Table */

	/* set up CPU registers and state */
	cpu_setregs();

	/* set up FPU state on the AP */
	npxinit();

	/* set up SSE registers */
	enable_sse();
}

/*******************************************************************
 * local functions and data
 */

/*
 * Start the SMP system
 */
static void
mp_start_aps(void *dummy __unused)
{
	if (lapic_enable) {
		/* start each Application Processor */
		start_all_aps(boot_address);
	} else {
		mp_bsp_simple_setup();
	}
}
SYSINIT(startaps, SI_BOOT2_START_APS, SI_ORDER_FIRST, mp_start_aps, NULL)

/*
 * start each AP in our list
 */
static int
start_all_aps(u_int boot_addr)
{
	int     x, i, pg;
	int	shift;
	int	smicount;
	int	smibest;
	int	smilast;
	u_char  mpbiosreason;
	u_long  mpbioswarmvec;
	struct mdglobaldata *gd;
	struct privatespace *ps;
	char *stack;
	uintptr_t kptbase;

	POSTCODE(START_ALL_APS_POST);

	/* install the AP 1st level boot code */
	install_ap_tramp(boot_addr);


	/* save the current value of the warm-start vector */
	mpbioswarmvec = *((u_long *) WARMBOOT_OFF);
	outb(CMOS_REG, BIOS_RESET);
	mpbiosreason = inb(CMOS_DATA);

	/* setup a vector to our boot code */
	*((volatile u_short *) WARMBOOT_OFF) = WARMBOOT_TARGET;
	*((volatile u_short *) WARMBOOT_SEG) = (boot_addr >> 4);
	outb(CMOS_REG, BIOS_RESET);
	outb(CMOS_DATA, BIOS_WARM);	/* 'warm-start' */

	/*
	 * If we have a TSC we can figure out the SMI interrupt rate.
	 * The SMI does not necessarily use a constant rate.  Spend
	 * up to 250ms trying to figure it out.
	 */
	smibest = 0;
	if (cpu_feature & CPUID_TSC) {
		set_apic_timer(275000);
		smilast = read_apic_timer();
		for (x = 0; x < 20 && read_apic_timer(); ++x) {
			smicount = smitest();
			if (smibest == 0 || smilast - smicount < smibest)
				smibest = smilast - smicount;
			smilast = smicount;
		}
		if (smibest > 250000)
			smibest = 0;
		if (smibest) {
			smibest = smibest * (int64_t)1000000 /
				  get_apic_timer_frequency();
		}
	}
	if (smibest)
		kprintf("SMI Frequency (worst case): %d Hz (%d us)\n",
			1000000 / smibest, smibest);


	/* set up temporary P==V mapping for AP boot */
	/* XXX this is a hack, we should boot the AP on its own stack/PTD */
	kptbase = (uintptr_t)(void *)KPTphys;
	for (x = 0; x < NKPT; x++) {
		PTD[x] = (pd_entry_t)(PG_V | PG_RW |
		    ((kptbase + x * PAGE_SIZE) & PG_FRAME));
	}
	cpu_invltlb();

	/* start each AP */
	for (x = 1; x <= naps; ++x) {

		/* This is a bit verbose, it will go away soon.  */

		/* first page of AP's private space */
		pg = x * i386_btop(sizeof(struct privatespace));

		/* allocate new private data page(s) */
		gd = (struct mdglobaldata *)kmem_alloc(&kernel_map, 
				MDGLOBALDATA_BASEALLOC_SIZE);
		/* wire it into the private page table page */
		for (i = 0; i < MDGLOBALDATA_BASEALLOC_SIZE; i += PAGE_SIZE) {
			SMPpt[pg + i / PAGE_SIZE] = (pt_entry_t)
			    (PG_V | PG_RW | vtophys_pte((char *)gd + i));
		}
		pg += MDGLOBALDATA_BASEALLOC_PAGES;

		SMPpt[pg + 0] = 0;		/* *gd_CMAP1 */
		SMPpt[pg + 1] = 0;		/* *gd_CMAP2 */
		SMPpt[pg + 2] = 0;		/* *gd_CMAP3 */
		SMPpt[pg + 3] = 0;		/* *gd_PMAP1 */

		/* allocate and set up an idle stack data page */
		stack = (char *)kmem_alloc(&kernel_map, UPAGES*PAGE_SIZE);
		for (i = 0; i < UPAGES; i++) {
			SMPpt[pg + 4 + i] = (pt_entry_t)
			    (PG_V | PG_RW | vtophys_pte(PAGE_SIZE * i + stack));
		}

		gd = &CPU_prvspace[x].mdglobaldata;	/* official location */
		bzero(gd, sizeof(*gd));
		gd->mi.gd_prvspace = ps = &CPU_prvspace[x];

		/* prime data page for it to use */
		mi_gdinit(&gd->mi, x);
		cpu_gdinit(gd, x);
		gd->gd_CMAP1 = &SMPpt[pg + 0];
		gd->gd_CMAP2 = &SMPpt[pg + 1];
		gd->gd_CMAP3 = &SMPpt[pg + 2];
		gd->gd_PMAP1 = &SMPpt[pg + 3];
		gd->gd_CADDR1 = ps->CPAGE1;
		gd->gd_CADDR2 = ps->CPAGE2;
		gd->gd_CADDR3 = ps->CPAGE3;
		gd->gd_PADDR1 = (unsigned *)ps->PPAGE1;

		/*
		 * Per-cpu pmap for get_ptbase().
		 */
		gd->gd_GDADDR1= (unsigned *)
			kmem_alloc_nofault(&kernel_map, SEG_SIZE, SEG_SIZE);
		gd->gd_GDMAP1 = &PTD[(vm_offset_t)gd->gd_GDADDR1 >> PDRSHIFT];

		gd->mi.gd_ipiq = (void *)kmem_alloc(&kernel_map, sizeof(lwkt_ipiq) * (naps + 1));
		bzero(gd->mi.gd_ipiq, sizeof(lwkt_ipiq) * (naps + 1));

		/*
		 * Setup the AP boot stack
		 */
		bootSTK = &ps->idlestack[UPAGES*PAGE_SIZE/2];
		bootAP = x;

		/* attempt to start the Application Processor */
		CHECK_INIT(99);	/* setup checkpoints */
		if (!start_ap(gd, boot_addr, smibest)) {
			kprintf("AP #%d (PHY# %d) failed!\n", x,
			    CPUID_TO_APICID(x));
			CHECK_PRINT("trace");	/* show checkpoints */
			/* better panic as the AP may be running loose */
			kprintf("panic y/n? [y] ");
			if (cngetc() != 'n')
				panic("bye-bye");
		}
		CHECK_PRINT("trace");		/* show checkpoints */
	}

	/* set ncpus to 1 + highest logical cpu.  Not all may have come up */
	ncpus = x;

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

	/* build our map of 'other' CPUs */
	mycpu->gd_other_cpus = smp_startup_mask & ~CPUMASK(mycpu->gd_cpuid);
	mycpu->gd_ipiq = (void *)kmem_alloc(&kernel_map, sizeof(lwkt_ipiq) * ncpus);
	bzero(mycpu->gd_ipiq, sizeof(lwkt_ipiq) * ncpus);

	/* restore the warmstart vector */
	*(u_long *) WARMBOOT_OFF = mpbioswarmvec;
	outb(CMOS_REG, BIOS_RESET);
	outb(CMOS_DATA, mpbiosreason);

	/*
	 * NOTE!  The idlestack for the BSP was setup by locore.  Finish
	 * up, clean out the P==V mapping we did earlier.
	 */
	for (x = 0; x < NKPT; x++)
		PTD[x] = 0;
	pmap_set_opt();

	/*
	 * Wait all APs to finish initializing LAPIC
	 */
	mp_finish_lapic = 1;
	if (bootverbose)
		kprintf("SMP: Waiting APs LAPIC initialization\n");
	if (cpu_feature & CPUID_TSC)
		tsc0_offset = rdtsc();
	tsc_offsets[0] = 0;
	rel_mplock();
	while (smp_lapic_mask != smp_startup_mask) {
		cpu_lfence();
		if (cpu_feature & CPUID_TSC)
			tsc0_offset = rdtsc();
	}
	while (try_mplock() == 0)
		;

	/* number of APs actually started */
	return ncpus - 1;
}

/*
 * load the 1st level AP boot code into base memory.
 */

/* targets for relocation */
extern void bigJump(void);
extern void bootCodeSeg(void);
extern void bootDataSeg(void);
extern void MPentry(void);
extern u_int MP_GDT;
extern u_int mp_gdtbase;

static void
install_ap_tramp(u_int boot_addr)
{
	int     x;
	int     size = *(int *) ((u_long) & bootMP_size);
	u_char *src = (u_char *) ((u_long) bootMP);
	u_char *dst = (u_char *) boot_addr + KERNBASE;
	u_int   boot_base = (u_int) bootMP;
	u_int8_t *dst8;
	u_int16_t *dst16;
	u_int32_t *dst32;

	POSTCODE(INSTALL_AP_TRAMP_POST);

	for (x = 0; x < size; ++x)
		*dst++ = *src++;

	/*
	 * modify addresses in code we just moved to basemem. unfortunately we
	 * need fairly detailed info about mpboot.s for this to work.  changes
	 * to mpboot.s might require changes here.
	 */

	/* boot code is located in KERNEL space */
	dst = (u_char *) boot_addr + KERNBASE;

	/* modify the lgdt arg */
	dst32 = (u_int32_t *) (dst + ((u_int) & mp_gdtbase - boot_base));
	*dst32 = boot_addr + ((u_int) & MP_GDT - boot_base);

	/* modify the ljmp target for MPentry() */
	dst32 = (u_int32_t *) (dst + ((u_int) bigJump - boot_base) + 1);
	*dst32 = ((u_int) MPentry - KERNBASE);

	/* modify the target for boot code segment */
	dst16 = (u_int16_t *) (dst + ((u_int) bootCodeSeg - boot_base));
	dst8 = (u_int8_t *) (dst16 + 1);
	*dst16 = boot_addr & 0xffff;
	*dst8 = (boot_addr >> 16) & 0xff;

	/* modify the target for boot data segment */
	dst16 = (u_int16_t *) (dst + ((u_int) bootDataSeg - boot_base));
	dst8 = (u_int8_t *) (dst16 + 1);
	*dst16 = boot_addr & 0xffff;
	*dst8 = (boot_addr >> 16) & 0xff;
}


/*
 * This function starts the AP (application processor) identified
 * by the APIC ID 'physicalCpu'.  It does quite a "song and dance"
 * to accomplish this.  This is necessary because of the nuances
 * of the different hardware we might encounter.  It ain't pretty,
 * but it seems to work.
 *
 * NOTE: eventually an AP gets to ap_init(), which is called just 
 * before the AP goes into the LWKT scheduler's idle loop.
 */
static int
start_ap(struct mdglobaldata *gd, u_int boot_addr, int smibest)
{
	int     physical_cpu;
	int     vector;
	u_long  icr_lo, icr_hi;

	POSTCODE(START_AP_POST);

	/* get the PHYSICAL APIC ID# */
	physical_cpu = CPUID_TO_APICID(gd->mi.gd_cpuid);

	/* calculate the vector */
	vector = (boot_addr >> 12) & 0xff;

	/* We don't want anything interfering */
	cpu_disable_intr();

	/* Make sure the target cpu sees everything */
	wbinvd();

	/*
	 * Try to detect when a SMI has occurred, wait up to 200ms.
	 *
	 * If a SMI occurs during an AP reset but before we issue
	 * the STARTUP command, the AP may brick.  To work around
	 * this problem we hold off doing the AP startup until
	 * after we have detected the SMI.  Hopefully another SMI
	 * will not occur before we finish the AP startup.
	 *
	 * Retries don't seem to help.  SMIs have a window of opportunity
	 * and if USB->legacy keyboard emulation is enabled in the BIOS
	 * the interrupt rate can be quite high.
	 *
	 * NOTE: Don't worry about the L1 cache load, it might bloat
	 *       ldelta a little but ndelta will be so huge when the SMI
	 *       occurs the detection logic will still work fine.
	 */
	if (smibest) {
		set_apic_timer(200000);
		smitest();
	}

	/*
	 * first we do an INIT/RESET IPI this INIT IPI might be run, reseting
	 * and running the target CPU. OR this INIT IPI might be latched (P5
	 * bug), CPU waiting for STARTUP IPI. OR this INIT IPI might be
	 * ignored.
	 *
	 * see apic/apicreg.h for icr bit definitions.
	 *
	 * TIME CRITICAL CODE, DO NOT DO ANY KPRINTFS IN THE HOT PATH.
	 */

	/*
	 * Setup the address for the target AP.  We can setup
	 * icr_hi once and then just trigger operations with
	 * icr_lo.
	 */
	icr_hi = lapic->icr_hi & ~APIC_ID_MASK;
	icr_hi |= (physical_cpu << 24);
	icr_lo = lapic->icr_lo & 0xfff00000;
	lapic->icr_hi = icr_hi;

	/*
	 * Do an INIT IPI: assert RESET
	 *
	 * Use edge triggered mode to assert INIT
	 */
	lapic->icr_lo = icr_lo | 0x0000c500;
	while (lapic->icr_lo & APIC_DELSTAT_MASK)
		 /* spin */ ;

	/*
	 * The spec calls for a 10ms delay but we may have to use a
	 * MUCH lower delay to avoid bricking an AP due to a fast SMI
	 * interrupt.  We have other loops here too and dividing by 2
	 * doesn't seem to be enough even after subtracting 350us,
	 * so we divide by 4.
	 *
	 * Our minimum delay is 150uS, maximum is 10ms.  If no SMI
	 * interrupt was detected we use the full 10ms.
	 */
	if (smibest == 0)
		u_sleep(10000);
	else if (smibest < 150 * 4 + 350)
		u_sleep(150);
	else if ((smibest - 350) / 4 < 10000)
		u_sleep((smibest - 350) / 4);
	else
		u_sleep(10000);

	/*
	 * Do an INIT IPI: deassert RESET
	 *
	 * Use level triggered mode to deassert.  It is unclear
	 * why we need to do this.
	 */
	lapic->icr_lo = icr_lo | 0x00008500;
	while (lapic->icr_lo & APIC_DELSTAT_MASK)
		 /* spin */ ;
	u_sleep(150);				/* wait 150us */

	/*
	 * Next we do a STARTUP IPI: the previous INIT IPI might still be
	 * latched, (P5 bug) this 1st STARTUP would then terminate
	 * immediately, and the previously started INIT IPI would continue. OR
	 * the previous INIT IPI has already run. and this STARTUP IPI will
	 * run. OR the previous INIT IPI was ignored. and this STARTUP IPI
	 * will run.
	 */
	lapic->icr_lo = icr_lo | 0x00000600 | vector;
	while (lapic->icr_lo & APIC_DELSTAT_MASK)
		 /* spin */ ;
	u_sleep(200);		/* wait ~200uS */

	/*
	 * Finally we do a 2nd STARTUP IPI: this 2nd STARTUP IPI should run IF
	 * the previous STARTUP IPI was cancelled by a latched INIT IPI. OR
	 * this STARTUP IPI will be ignored, as only ONE STARTUP IPI is
	 * recognized after hardware RESET or INIT IPI.
	 */
	lapic->icr_lo = icr_lo | 0x00000600 | vector;
	while (lapic->icr_lo & APIC_DELSTAT_MASK)
		 /* spin */ ;

	/* Resume normal operation */
	cpu_enable_intr();

	/* wait for it to start, see ap_init() */
	set_apic_timer(5000000);/* == 5 seconds */
	while (read_apic_timer()) {
		if (smp_startup_mask & CPUMASK(gd->mi.gd_cpuid))
			return 1;	/* return SUCCESS */
	}

	return 0;		/* return FAILURE */
}

static
int
smitest(void)
{
	int64_t	ltsc;
	int64_t	ntsc;
	int64_t	ldelta;
	int64_t	ndelta;
	int count;

	ldelta = 0;
	ndelta = 0;
	while (read_apic_timer()) {
		ltsc = rdtsc();
		for (count = 0; count < 100; ++count)
			ntsc = rdtsc();	/* force loop to occur */
		if (ldelta) {
			ndelta = ntsc - ltsc;
			if (ldelta > ndelta)
				ldelta = ndelta;
			if (ndelta > ldelta * 2)
				break;
		} else {
			ldelta = ntsc - ltsc;
		}
	}
	return(read_apic_timer());
}

/*
 * Lazy flush the TLB on all other CPU's.  DEPRECATED.
 *
 * If for some reason we were unable to start all cpus we cannot safely
 * use broadcast IPIs.
 */

static cpumask_t smp_invltlb_req;
#define SMP_INVLTLB_DEBUG

void
smp_invltlb(void)
{
	struct mdglobaldata *md = mdcpu;
#ifdef SMP_INVLTLB_DEBUG
	long count = 0;
	long xcount = 0;
#endif

	crit_enter_gd(&md->mi);
	md->gd_invltlb_ret = 0;
	++md->mi.gd_cnt.v_smpinvltlb;
	atomic_set_cpumask(&smp_invltlb_req, md->mi.gd_cpumask);
#ifdef SMP_INVLTLB_DEBUG
again:
#endif
	if (smp_startup_mask == smp_active_mask) {
		all_but_self_ipi(XINVLTLB_OFFSET);
	} else {
		selected_apic_ipi(smp_active_mask & ~md->mi.gd_cpumask,
				  XINVLTLB_OFFSET, APIC_DELMODE_FIXED);
	}

#ifdef SMP_INVLTLB_DEBUG
	if (xcount)
		kprintf("smp_invltlb: ipi sent\n");
#endif
	while ((md->gd_invltlb_ret & smp_active_mask & ~md->mi.gd_cpumask) !=
	       (smp_active_mask & ~md->mi.gd_cpumask)) {
		cpu_mfence();
		cpu_pause();
#ifdef SMP_INVLTLB_DEBUG
		/* DEBUGGING */
		if (++count == 400000000) {
			print_backtrace(-1);
			kprintf("smp_invltlb: endless loop %08lx %08lx, "
				"eflags %016lx retry",
				(long)md->gd_invltlb_ret,
				(long)smp_invltlb_req,
				(long)read_eflags());
			__asm __volatile ("sti");
			++xcount;
			if (xcount > 2)
				lwkt_process_ipiq();
			if (xcount > 3) {
				int bcpu = BSFCPUMASK(~md->gd_invltlb_ret &
						      ~md->mi.gd_cpumask &
						      smp_active_mask);
				globaldata_t xgd;
				kprintf("bcpu %d\n", bcpu);
				xgd = globaldata_find(bcpu);
				kprintf("thread %p %s\n", xgd->gd_curthread, xgd->gd_curthread->td_comm);
			}
			if (xcount > 5)
				panic("giving up");
			count = 0;
			goto again;
		}
#endif
	}
	atomic_clear_cpumask(&smp_invltlb_req, md->mi.gd_cpumask);
	crit_exit_gd(&md->mi);
}

/*
 * Called from Xinvltlb assembly with interrupts disabled.  We didn't
 * bother to bump the critical section count or nested interrupt count
 * so only do very low level operations here.
 */
void
smp_invltlb_intr(void)
{
	struct mdglobaldata *md = mdcpu;
	struct mdglobaldata *omd;
	cpumask_t mask;
	int cpu;

	mask = smp_invltlb_req;
	cpu_mfence();
	cpu_invltlb();
	while (mask) {
		cpu = BSFCPUMASK(mask);
		mask &= ~CPUMASK(cpu);
		omd = (struct mdglobaldata *)globaldata_find(cpu);
		atomic_set_cpumask(&omd->gd_invltlb_ret, md->mi.gd_cpumask);
	}
}

void
cpu_wbinvd_on_all_cpus_callback(void *arg)
{
    wbinvd();
}

/*
 * When called the executing CPU will send an IPI to all other CPUs
 *  requesting that they halt execution.
 *
 * Usually (but not necessarily) called with 'other_cpus' as its arg.
 *
 *  - Signals all CPUs in map to stop.
 *  - Waits for each to stop.
 *
 * Returns:
 *  -1: error
 *   0: NA
 *   1: ok
 *
 * XXX FIXME: this is not MP-safe, needs a lock to prevent multiple CPUs
 *            from executing at same time.
 */
int
stop_cpus(cpumask_t map)
{
	map &= smp_active_mask;

	/* send the Xcpustop IPI to all CPUs in map */
	selected_apic_ipi(map, XCPUSTOP_OFFSET, APIC_DELMODE_FIXED);
	
	while ((stopped_cpus & map) != map)
		/* spin */ ;

	return 1;
}


/*
 * Called by a CPU to restart stopped CPUs. 
 *
 * Usually (but not necessarily) called with 'stopped_cpus' as its arg.
 *
 *  - Signals all CPUs in map to restart.
 *  - Waits for each to restart.
 *
 * Returns:
 *  -1: error
 *   0: NA
 *   1: ok
 */
int
restart_cpus(cpumask_t map)
{
	/* signal other cpus to restart */
	started_cpus = map & smp_active_mask;

	while ((stopped_cpus & map) != 0) /* wait for each to clear its bit */
		/* spin */ ;

	return 1;
}

/*
 * This is called once the mpboot code has gotten us properly relocated
 * and the MMU turned on, etc.   ap_init() is actually the idle thread,
 * and when it returns the scheduler will call the real cpu_idle() main
 * loop for the idlethread.  Interrupts are disabled on entry and should
 * remain disabled at return.
 */
void
ap_init(void)
{
	int	cpu_id;

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
	 * Interlock for LAPIC initialization.  Wait until mp_finish_lapic is
	 * non-zero, then get the MP lock.
	 *
	 * Note: We are in a critical section.
	 *
	 * Note: we are the idle thread, we can only spin.
	 *
	 * Note: The load fence is memory volatile and prevents the compiler
	 * from improperly caching mp_finish_lapic, and the cpu from improperly
	 * caching it.
	 */
	while (mp_finish_lapic == 0)
		cpu_lfence();
	while (try_mplock() == 0)
		;

	if (cpu_feature & CPUID_TSC) {
		/*
		 * The BSP is constantly updating tsc0_offset, figure out
		 * the relative difference to synchronize ktrdump.
		 */
		tsc_offsets[mycpu->gd_cpuid] = rdtsc() - tsc0_offset;
	}

	/* BSP may have changed PTD while we're waiting for the lock */
	cpu_invltlb();

#if defined(I586_CPU) && !defined(NO_F00F_HACK)
	lidt(&r_idt);
#endif

	/* Build our map of 'other' CPUs. */
	mycpu->gd_other_cpus = smp_startup_mask & ~CPUMASK(mycpu->gd_cpuid);

	/* A quick check from sanity claus */
	cpu_id = APICID_TO_CPUID((lapic->id & 0xff000000) >> 24);
	if (mycpu->gd_cpuid != cpu_id) {
		kprintf("SMP: assigned cpuid = %d\n", mycpu->gd_cpuid);
		kprintf("SMP: actual cpuid = %d\n", cpu_id);
		kprintf("PTD[MPPTDI] = %p\n", (void *)PTD[MPPTDI]);
		panic("cpuid mismatch! boom!!");
	}

	/* Initialize AP's local APIC for irq's */
	lapic_init(FALSE);

	/* LAPIC initialization is done */
	smp_lapic_mask |= CPUMASK(mycpu->gd_cpuid);
	cpu_mfence();

	/* Let BSP move onto the next initialization stage */
	rel_mplock();

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
	while (mp_finish == 0)
		cpu_lfence();
	while (try_mplock() == 0)
		;

	/* BSP may have changed PTD while we're waiting for the lock */
	cpu_invltlb();

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

	/*
	 * Enable interrupts here.  idle_restore will also do it, but
	 * doing it here lets us clean up any strays that got posted to
	 * the CPU during the AP boot while we are still in a critical
	 * section.
	 */
	__asm __volatile("sti; pause; pause"::);
	bzero(mdcpu->gd_ipending, sizeof(mdcpu->gd_ipending));

	initclocks_pcpu();	/* clock interrupts (via IPIs) */
	lwkt_process_ipiq();

	/*
	 * Releasing the mp lock lets the BSP finish up the SMP init
	 */
	rel_mplock();
	KKASSERT((curthread->td_flags & TDF_RUNQ) == 0);
}

/*
 * Get SMP fully working before we start initializing devices.
 */
static
void
ap_finish(void)
{
	mp_finish = 1;
	if (bootverbose)
		kprintf("Finish MP startup\n");
	rel_mplock();
	while (smp_active_mask != smp_startup_mask)
		cpu_lfence();
	while (try_mplock() == 0)
		;
	if (bootverbose)
		kprintf("Active CPU Mask: %08x\n", smp_active_mask);
}

SYSINIT(finishsmp, SI_BOOT2_FINISH_SMP, SI_ORDER_FIRST, ap_finish, NULL)

void
cpu_send_ipiq(int dcpu)
{
        if (CPUMASK(dcpu) & smp_active_mask)
                single_apic_ipi(dcpu, XIPIQ_OFFSET, APIC_DELMODE_FIXED);
}

#if 0	/* single_apic_ipi_passive() not working yet */
/*
 * Returns 0 on failure, 1 on success
 */
int
cpu_send_ipiq_passive(int dcpu)
{
        int r = 0;
        if (CPUMASK(dcpu) & smp_active_mask) {
                r = single_apic_ipi_passive(dcpu, XIPIQ_OFFSET,
                                        APIC_DELMODE_FIXED);
        }
	return(r);
}
#endif

static void
mp_bsp_simple_setup(void)
{
	/* build our map of 'other' CPUs */
	mycpu->gd_other_cpus = smp_startup_mask & ~CPUMASK(mycpu->gd_cpuid);
	mycpu->gd_ipiq = (void *)kmem_alloc(&kernel_map, sizeof(lwkt_ipiq) * ncpus);
	bzero(mycpu->gd_ipiq, sizeof(lwkt_ipiq) * ncpus);

	pmap_set_opt();

	if (cpu_feature & CPUID_TSC)
		tsc0_offset = rdtsc();
}


/*
 * CPU TOPOLOGY DETECTION FUNCTIONS
 */

/* Detect intel topology using CPUID 
 * Ref: http://www.intel.com/Assets/PDF/appnote/241618.pdf, pg 41
 */
static void
detect_intel_topology(int count_htt_cores)
{
	int shift = 0;
	int ecx_index = 0;
	int core_plus_logical_bits = 0;
	int cores_per_package;
	int logical_per_package;
	int logical_per_core;
	unsigned int p[4];

	if (cpu_high >= 0xb) {
		goto FUNC_B;

	} else if (cpu_high >= 0x4) {
		goto FUNC_4;

	} else {
		core_bits = 0;
		for (shift = 0; (1 << shift) < count_htt_cores; ++shift)
			;
		logical_CPU_bits = 1 << shift;
		return;
	}

FUNC_B:
	cpuid_count(0xb, FUNC_B_THREAD_LEVEL, p);

	/* if 0xb not supported - fallback to 0x4 */
	if (p[1] == 0 || (FUNC_B_TYPE(p[2]) != FUNC_B_THREAD_TYPE)) {
		goto FUNC_4;
	}

	logical_CPU_bits = FUNC_B_BITS_SHIFT_NEXT_LEVEL(p[0]);

	ecx_index = FUNC_B_THREAD_LEVEL + 1;
	do {
		cpuid_count(0xb, ecx_index, p);
		/* Check for the Core type in the implemented sub leaves. */
		if (FUNC_B_TYPE(p[2]) == FUNC_B_CORE_TYPE) {
			core_plus_logical_bits = FUNC_B_BITS_SHIFT_NEXT_LEVEL(p[0]);
			break;
		}
		ecx_index++;
	} while (FUNC_B_TYPE(p[2]) != FUNC_B_INVALID_TYPE);

	core_bits = core_plus_logical_bits - logical_CPU_bits;

	return;

FUNC_4:
	cpuid_count(0x4, 0, p);
	cores_per_package = FUNC_4_MAX_CORE_NO(p[0]) + 1;
	
	logical_per_package = count_htt_cores;
	logical_per_core = logical_per_package / cores_per_package;
	
	for (shift = 0; (1 << shift) < logical_per_core; ++shift)
		;
	logical_CPU_bits = shift;

	for (shift = 0; (1 << shift) < cores_per_package; ++shift)
		;
	core_bits = shift;

	return;
}

/* Detect AMD topology using CPUID 
 * Ref: http://support.amd.com/us/Embedded_TechDocs/25481.pdf, last page
 */
static void
detect_amd_topology(int count_htt_cores)
{
	int shift = 0;

	if ((cpu_feature & CPUID_HTT)
	    && (amd_feature2 & AMDID2_CMP)) {

		if (cpu_procinfo2 & AMDID_COREID_SIZE) {
			core_bits = (cpu_procinfo2 & AMDID_COREID_SIZE)
				>> AMDID_COREID_SIZE_SHIFT;
		} else {
			core_bits = (cpu_procinfo2 & AMDID_CMP_CORES) + 1;
			for (shift = 0; (1 << shift) < core_bits; ++shift);
			core_bits = shift;
		}

		logical_CPU_bits = count_htt_cores >> core_bits;
		for (shift = 0; (1 << shift) < logical_CPU_bits; ++shift)
			;
		logical_CPU_bits = shift;
	} else {
		for (shift = 0; (1 << shift) < count_htt_cores; ++shift)
			;
		core_bits = shift;
		logical_CPU_bits = 0;
	}
}

/* Calculate
 * - logical_CPU_bits
 * - core_bits
 * With the values above (for AMD or INTEL) we are able to generally
 * detect the CPU topology (number of cores for each level):
 * Ref: http://wiki.osdev.org/Detecting_CPU_Topology_(80x86)
 * Ref: http://www.multicoreinfo.com/research/papers/whitepapers/Intel-detect-topology.pdf
 */
void
detect_cpu_topology(void)
{
	static int topology_detected = 0;
	int count = 0;
	
	if (topology_detected) {
		goto OUT;
	}

	if ((cpu_feature & CPUID_HTT) == 0) {
		core_bits = 0;
		logical_CPU_bits = 0;
		goto OUT;
	} else {
		count = (cpu_procinfo & CPUID_HTT_CORES)
		    >> CPUID_HTT_CORE_SHIFT;
	}	

	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		detect_intel_topology(count);	
	} else if (cpu_vendor_id == CPU_VENDOR_AMD) {
		detect_amd_topology(count);
	}

OUT:
	if (bootverbose)
		kprintf("Bits within APICID: logical_CPU_bits: %d; core_bits: %d\n",
		    logical_CPU_bits, core_bits);

	topology_detected = 1;
}

/* Interface functions to calculate chip_ID,
 * core_number and logical_number
 * Ref: http://wiki.osdev.org/Detecting_CPU_Topology_(80x86)
 */
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
