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

#include <sys/mplock2.h>

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

#include <machine/smp.h>
#include <machine_base/apic/apicreg.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine_base/apic/mpapic.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/tss.h>
#include <machine/specialreg.h>
#include <machine/globaldata.h>
#include <machine/pmap_inval.h>

#include <machine/md_var.h>		/* setidt() */
#include <machine_base/icu/icu.h>	/* IPIs */
#include <machine_base/apic/ioapic_abi.h>
#include <machine/intr_machdep.h>	/* IPIs */

#define WARMBOOT_TARGET		0
#define WARMBOOT_OFF		(KERNBASE + 0x0467)
#define WARMBOOT_SEG		(KERNBASE + 0x0469)

#define BIOS_BASE		(0xf0000)
#define BIOS_BASE2		(0xe0000)
#define BIOS_SIZE		(0x10000)
#define BIOS_COUNT		(BIOS_SIZE/4)

#define CMOS_REG		(0x70)
#define CMOS_DATA		(0x71)
#define BIOS_RESET		(0x0f)
#define BIOS_WARM		(0x0a)

#define PROCENTRY_FLAG_EN	0x01
#define PROCENTRY_FLAG_BP	0x02
#define IOAPICENTRY_FLAG_EN	0x01


/* MP Floating Pointer Structure */
typedef struct MPFPS {
	char    signature[4];
	u_int32_t pap;
	u_char  length;
	u_char  spec_rev;
	u_char  checksum;
	u_char  mpfb1;
	u_char  mpfb2;
	u_char  mpfb3;
	u_char  mpfb4;
	u_char  mpfb5;
}      *mpfps_t;

/* MP Configuration Table Header */
typedef struct MPCTH {
	char    signature[4];
	u_short base_table_length;
	u_char  spec_rev;
	u_char  checksum;
	u_char  oem_id[8];
	u_char  product_id[12];
	u_int32_t oem_table_pointer;
	u_short oem_table_size;
	u_short entry_count;
	u_int32_t apic_address;
	u_short extended_table_length;
	u_char  extended_table_checksum;
	u_char  reserved;
}      *mpcth_t;


typedef struct PROCENTRY {
	u_char  type;
	u_char  apic_id;
	u_char  apic_version;
	u_char  cpu_flags;
	u_int32_t cpu_signature;
	u_int32_t feature_flags;
	u_int32_t reserved1;
	u_int32_t reserved2;
}      *proc_entry_ptr;

typedef struct BUSENTRY {
	u_char  type;
	u_char  bus_id;
	char    bus_type[6];
}      *bus_entry_ptr;

typedef struct IOAPICENTRY {
	u_char  type;
	u_char  apic_id;
	u_char  apic_version;
	u_char  apic_flags;
	u_int32_t apic_address;
}      *io_apic_entry_ptr;

typedef struct INTENTRY {
	u_char  type;
	u_char  int_type;
	u_short int_flags;
	u_char  src_bus_id;
	u_char  src_bus_irq;
	u_char  dst_apic_id;
	u_char  dst_apic_int;
}      *int_entry_ptr;

/* descriptions of MP basetable entries */
typedef struct BASETABLE_ENTRY {
	u_char  type;
	u_char  length;
	char    name[16];
}       basetable_entry;

struct mptable_pos {
	mpfps_t		mp_fps;
	mpcth_t		mp_cth;
	vm_size_t	mp_cth_mapsz;	
};

#define MPTABLE_POS_USE_DEFAULT(mpt) \
	((mpt)->mp_fps->mpfb1 != 0 || (mpt)->mp_cth == NULL)

struct mptable_bus {
	int		mb_id;
	int		mb_type;	/* MPTABLE_BUS_ */
	TAILQ_ENTRY(mptable_bus) mb_link;
};

#define MPTABLE_BUS_ISA		0
#define MPTABLE_BUS_PCI		1

struct mptable_bus_info {
	TAILQ_HEAD(, mptable_bus) mbi_list;
};

struct mptable_pci_int {
	int		mpci_bus;
	int		mpci_dev;
	int		mpci_pin;

	int		mpci_ioapic_idx;
	int		mpci_ioapic_pin;
	TAILQ_ENTRY(mptable_pci_int) mpci_link;
};

struct mptable_ioapic {
	int		mio_idx;
	int		mio_apic_id;
	uint32_t	mio_addr;
	int		mio_gsi_base;
	int		mio_npin;
	TAILQ_ENTRY(mptable_ioapic) mio_link;
};

typedef	int	(*mptable_iter_func)(void *, const void *, int);

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

int	mp_naps;		/* # of Applications processors */
extern	int nkpt;

u_int32_t cpu_apic_versions[NAPICID];	/* populated during mptable scan */
int64_t tsc0_offset;
extern int64_t tsc_offsets[];

extern u_long ebda_addr;

#ifdef SMP /* APIC-IO */
struct apic_intmapinfo	int_to_apicintpin[APIC_INTMAPSIZE];
#endif

/*
 * APIC ID logical/physical mapping structures.
 * We oversize these to simplify boot-time config.
 */
int     cpu_num_to_apic_id[NAPICID];
int     apic_id_to_logical[NAPICID];

/* AP uses this during bootstrap.  Do not staticize.  */
char *bootSTK;
static int bootAP;

struct pcb stoppcbs[MAXCPU];

extern inthand_t IDTVEC(fast_syscall), IDTVEC(fast_syscall32);

static basetable_entry basetable_entry_types[] =
{
	{0, 20, "Processor"},
	{1, 8, "Bus"},
	{2, 8, "I/O APIC"},
	{3, 8, "I/O INT"},
	{4, 8, "Local INT"}
};

/*
 * Local data and functions.
 */

static u_int	boot_address;
static u_int	base_memory;
static int	mp_finish;
static int	mp_finish_lapic;

static void	mp_enable(u_int boot_addr);

static int	mptable_iterate_entries(const mpcth_t,
		    mptable_iter_func, void *);
static int	mptable_search(void);
static long	mptable_search_sig(u_int32_t target, int count);
static int	mptable_hyperthread_fixup(cpumask_t, int);
static int	mptable_map(struct mptable_pos *);
static void	mptable_unmap(struct mptable_pos *);
static void	mptable_bus_info_alloc(const mpcth_t,
		    struct mptable_bus_info *);
static void	mptable_bus_info_free(struct mptable_bus_info *);

static int	mptable_lapic_probe(struct lapic_enumerator *);
static void	mptable_lapic_enumerate(struct lapic_enumerator *);
static void	mptable_lapic_default(void);

static int	mptable_ioapic_probe(struct ioapic_enumerator *);
static void	mptable_ioapic_enumerate(struct ioapic_enumerator *);

static int	start_all_aps(u_int boot_addr);
#if 0
static void	install_ap_tramp(u_int boot_addr);
#endif
static int	start_ap(struct mdglobaldata *gd, u_int boot_addr, int smibest);
static int	smitest(void);

static cpumask_t smp_startup_mask = 1;	/* which cpus have been started */
static cpumask_t smp_lapic_mask = 1;	/* which cpus have lapic been inited */
cpumask_t smp_active_mask = 1;	/* which cpus are ready for IPIs etc? */
SYSCTL_INT(_machdep, OID_AUTO, smp_active, CTLFLAG_RD, &smp_active_mask, 0, "");
static u_int	bootMP_size;

int			imcr_present;

static vm_paddr_t	mptable_fps_phyaddr;
static int		mptable_use_default;
static TAILQ_HEAD(mptable_pci_int_list, mptable_pci_int) mptable_pci_int_list =
	TAILQ_HEAD_INITIALIZER(mptable_pci_int_list);
static TAILQ_HEAD(mptable_ioapic_list, mptable_ioapic) mptable_ioapic_list =
	TAILQ_HEAD_INITIALIZER(mptable_ioapic_list);

/*
 * Calculate usable address in base memory for AP trampoline code.
 */
u_int
mp_bootaddress(u_int basemem)
{
	POSTCODE(MP_BOOTADDRESS_POST);

	base_memory = basemem;

	bootMP_size = mptramp_end - mptramp_start;
	boot_address = trunc_page(basemem * 1024); /* round down to 4k boundary */
	if (((basemem * 1024) - boot_address) < bootMP_size)
		boot_address -= PAGE_SIZE;	/* not enough, lower by 4k */
	/* 3 levels of page table pages */
	mptramp_pagetables = boot_address - (PAGE_SIZE * 3);

	return mptramp_pagetables;
}


static void
mptable_probe(void)
{
	struct mptable_pos mpt;
	int error;

	KKASSERT(mptable_fps_phyaddr == 0);

	mptable_fps_phyaddr = mptable_search();
	if (mptable_fps_phyaddr == 0)
		return;

	error = mptable_map(&mpt);
	if (error) {
		mptable_fps_phyaddr = 0;
		return;
	}

	if (MPTABLE_POS_USE_DEFAULT(&mpt)) {
		kprintf("MPTABLE: use default configuration\n");
		mptable_use_default = 1;
	}
	if (mpt.mp_fps->mpfb2 & 0x80)
		imcr_present = 1;

	mptable_unmap(&mpt);
}
SYSINIT(mptable_probe, SI_BOOT2_PRESMP, SI_ORDER_FIRST, mptable_probe, 0);

/*
 * Look for an Intel MP spec table (ie, SMP capable hardware).
 */
static int
mptable_search(void)
{
	long    x;
	u_int32_t target;
 
	POSTCODE(MP_PROBE_POST);

	/* see if EBDA exists */
	if (ebda_addr != 0) {
		/* search first 1K of EBDA */
		target = (u_int32_t)ebda_addr;
		if ((x = mptable_search_sig(target, 1024 / 4)) > 0)
			return x;
	} else {
		/* last 1K of base memory, effective 'top of base' passed in */
		target = (u_int32_t)(base_memory - 0x400);
		if ((x = mptable_search_sig(target, 1024 / 4)) > 0)
			return x;
	}

	/* search the BIOS */
	target = (u_int32_t)BIOS_BASE;
	if ((x = mptable_search_sig(target, BIOS_COUNT)) > 0)
		return x;

	/* search the extended BIOS */
	target = (u_int32_t)BIOS_BASE2;
	if ((x = mptable_search_sig(target, BIOS_COUNT)) > 0)
		return x;

	/* nothing found */
	return 0;
}

static int
mptable_iterate_entries(const mpcth_t cth, mptable_iter_func func, void *arg)
{
	int count, total_size;
	const void *position;

	KKASSERT(cth->base_table_length >= sizeof(struct MPCTH));
	total_size = cth->base_table_length - sizeof(struct MPCTH);
	position = (const uint8_t *)cth + sizeof(struct MPCTH);
	count = cth->entry_count;

	while (count--) {
		int type, error;

		KKASSERT(total_size >= 0);
		if (total_size == 0) {
			kprintf("invalid base MP table, "
				"entry count and length mismatch\n");
			return EINVAL;
		}

		type = *(const uint8_t *)position;
		switch (type) {
		case 0: /* processor_entry */
		case 1: /* bus_entry */
		case 2: /* io_apic_entry */
		case 3: /* int_entry */
		case 4:	/* int_entry */
			break;
		default:
			kprintf("unknown base MP table entry type %d\n", type);
			return EINVAL;
		}

		if (total_size < basetable_entry_types[type].length) {
			kprintf("invalid base MP table length, "
				"does not contain all entries\n");
			return EINVAL;
		}
		total_size -= basetable_entry_types[type].length;

		error = func(arg, position, type);
		if (error)
			return error;

		position = (const uint8_t *)position +
		    basetable_entry_types[type].length;
	}
	return 0;
}


/*
 * Startup the SMP processors.
 */
void
mp_start(void)
{
	POSTCODE(MP_START_POST);
	mp_enable(boot_address);
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
	kprintf(" cpu0 (BSP): apic id: %2d", CPU_TO_ID(0));
	kprintf(", version: 0x%08x\n", cpu_apic_versions[0]);
	for (x = 1; x <= mp_naps; ++x) {
		kprintf(" cpu%d (AP):  apic id: %2d", x, CPU_TO_ID(x));
		kprintf(", version: 0x%08x\n", cpu_apic_versions[x]);
	}

	if (!apic_io_enable)
		kprintf(" Warning: APIC I/O disabled\n");
}

/*
 * AP cpu's call this to sync up protected mode.
 *
 * WARNING! %gs is not set up on entry.  This routine sets up %gs.
 */
void
init_secondary(void)
{
	int	gsel_tss;
	int	x, myid = bootAP;
	u_int64_t msr, cr0;
	struct mdglobaldata *md;
	struct privatespace *ps;

	ps = &CPU_prvspace[myid];

	gdt_segs[GPROC0_SEL].ssd_base =
		(long) &ps->mdglobaldata.gd_common_tss;
	ps->mdglobaldata.mi.gd_prvspace = ps;

	/* We fill the 32-bit segment descriptors */
	for (x = 0; x < NGDT; x++) {
		if (x != GPROC0_SEL && x != (GPROC0_SEL + 1))
			ssdtosd(&gdt_segs[x], &gdt[myid * NGDT + x]);
	}
	/* And now a 64-bit one */
	ssdtosyssd(&gdt_segs[GPROC0_SEL],
	    (struct system_segment_descriptor *)&gdt[myid * NGDT + GPROC0_SEL]);

	r_gdt.rd_limit = NGDT * sizeof(gdt[0]) - 1;
	r_gdt.rd_base = (long) &gdt[myid * NGDT];
	lgdt(&r_gdt);			/* does magic intra-segment return */

	/* lgdt() destroys the GSBASE value, so we load GSBASE after lgdt() */
	wrmsr(MSR_FSBASE, 0);		/* User value */
	wrmsr(MSR_GSBASE, (u_int64_t)ps);
	wrmsr(MSR_KGSBASE, 0);		/* XXX User value while we're in the kernel */

	lidt(&r_idt);

#if 0
	lldt(_default_ldt);
	mdcpu->gd_currentldt = _default_ldt;
#endif

	gsel_tss = GSEL(GPROC0_SEL, SEL_KPL);
	gdt[myid * NGDT + GPROC0_SEL].sd_type = SDT_SYSTSS;

	md = mdcpu;	/* loaded through %gs:0 (mdglobaldata.mi.gd_prvspace)*/

	md->gd_common_tss.tss_rsp0 = 0;	/* not used until after switch */
#if 0 /* JG XXX */
	md->gd_common_tss.tss_ioopt = (sizeof md->gd_common_tss) << 16;
#endif
	md->gd_tss_gdt = &gdt[myid * NGDT + GPROC0_SEL];
	md->gd_common_tssd = *md->gd_tss_gdt;

	/* double fault stack */
	md->gd_common_tss.tss_ist1 =
		(long)&md->mi.gd_prvspace->idlestack[
			sizeof(md->mi.gd_prvspace->idlestack)];

	ltr(gsel_tss);

	/*
	 * Set to a known state:
	 * Set by mpboot.s: CR0_PG, CR0_PE
	 * Set by cpu_setregs: CR0_NE, CR0_MP, CR0_TS, CR0_WP, CR0_AM
	 */
	cr0 = rcr0();
	cr0 &= ~(CR0_CD | CR0_NW | CR0_EM);
	load_cr0(cr0);

	/* Set up the fast syscall stuff */
	msr = rdmsr(MSR_EFER) | EFER_SCE;
	wrmsr(MSR_EFER, msr);
	wrmsr(MSR_LSTAR, (u_int64_t)IDTVEC(fast_syscall));
	wrmsr(MSR_CSTAR, (u_int64_t)IDTVEC(fast_syscall32));
	msr = ((u_int64_t)GSEL(GCODE_SEL, SEL_KPL) << 32) |
	      ((u_int64_t)GSEL(GUCODE32_SEL, SEL_UPL) << 48);
	wrmsr(MSR_STAR, msr);
	wrmsr(MSR_SF_MASK, PSL_NT|PSL_T|PSL_I|PSL_C|PSL_D);

	pmap_set_opt();		/* PSE/4MB pages, etc */
#if JGXXX
	/* Initialize the PAT MSR. */
	pmap_init_pat();
#endif

	/* set up CPU registers and state */
	cpu_setregs();

	/* set up SSE/NX registers */
	initializecpu();

	/* set up FPU state on the AP */
	npxinit(__INITIAL_NPXCW__);

	/* disable the APIC, just to be SURE */
	lapic->svr &= ~APIC_SVR_ENABLE;

	/* data returned to BSP */
	cpu_apic_versions[0] = lapic->version;
}

/*******************************************************************
 * local functions and data
 */

/*
 * start the SMP system
 */
static void
mp_enable(u_int boot_addr)
{
	POSTCODE(MP_ENABLE_POST);

	lapic_config();

	/* Initialize BSP's local APIC */
	lapic_init(TRUE);

	/* start each Application Processor */
	start_all_aps(boot_addr);

	if (apic_io_enable)
		ioapic_config();

	/* Finalize PIC */
	MachIntrABI.finalize();
}


/*
 * look for the MP spec signature
 */

/* string defined by the Intel MP Spec as identifying the MP table */
#define MP_SIG		0x5f504d5f	/* _MP_ */
#define NEXT(X)		((X) += 4)
static long
mptable_search_sig(u_int32_t target, int count)
{
	vm_size_t map_size;
	u_int32_t *addr;
	int x, ret;

	KKASSERT(target != 0);

	map_size = count * sizeof(u_int32_t);
	addr = pmap_mapdev((vm_paddr_t)target, map_size);

	ret = 0;
	for (x = 0; x < count; NEXT(x)) {
		if (addr[x] == MP_SIG) {
			/* make array index a byte index */
			ret = target + (x * sizeof(u_int32_t));
			break;
		}
	}

	pmap_unmapdev((vm_offset_t)addr, map_size);
	return ret;
}

static int processor_entry	(const struct PROCENTRY *entry, int cpu);

/*
 * Check if we should perform a hyperthreading "fix-up" to
 * enumerate any logical CPU's that aren't already listed
 * in the table.
 *
 * XXX: We assume that all of the physical CPUs in the
 * system have the same number of logical CPUs.
 *
 * XXX: We assume that APIC ID's are allocated such that
 * the APIC ID's for a physical processor are aligned
 * with the number of logical CPU's in the processor.
 */
static int
mptable_hyperthread_fixup(cpumask_t id_mask, int cpu_count)
{
	int i, id, lcpus_max, logical_cpus;

	if ((cpu_feature & CPUID_HTT) == 0)
		return 0;

	lcpus_max = (cpu_procinfo & CPUID_HTT_CORES) >> 16;
	if (lcpus_max <= 1)
		return 0;

	if (strcmp(cpu_vendor, "GenuineIntel") == 0) {
		/*
		 * INSTRUCTION SET REFERENCE, A-M (#253666)
		 * Page 3-181, Table 3-20
		 * "The nearest power-of-2 integer that is not smaller
		 *  than EBX[23:16] is the number of unique initial APIC
		 *  IDs reserved for addressing different logical
		 *  processors in a physical package."
		 */
		for (i = 0; ; ++i) {
			if ((1 << i) >= lcpus_max) {
				lcpus_max = 1 << i;
				break;
			}
		}
	}

	KKASSERT(cpu_count != 0);
	if (cpu_count == lcpus_max) {
		/* We have nothing to fix */
		return 0;
	} else if (cpu_count == 1) {
		/* XXX this may be incorrect */
		logical_cpus = lcpus_max;
	} else {
		int cur, prev, dist;

		/*
		 * Calculate the distances between two nearest
		 * APIC IDs.  If all such distances are same,
		 * then it is the number of missing cpus that
		 * we are going to fill later.
		 */
		dist = cur = prev = -1;
		for (id = 0; id < MAXCPU; ++id) {
			if ((id_mask & CPUMASK(id)) == 0)
				continue;

			cur = id;
			if (prev >= 0) {
				int new_dist = cur - prev;

				if (dist < 0)
					dist = new_dist;

				/*
				 * Make sure that all distances
				 * between two nearest APIC IDs
				 * are same.
				 */
				if (dist != new_dist)
					return 0;
			}
			prev = cur;
		}
		if (dist == 1)
			return 0;

		/* Must be power of 2 */
		if (dist & (dist - 1))
			return 0;

		/* Can't exceed CPU package capacity */
		if (dist > lcpus_max)
			logical_cpus = lcpus_max;
		else
			logical_cpus = dist;
	}

	/*
	 * For each APIC ID of a CPU that is set in the mask,
	 * scan the other candidate APIC ID's for this
	 * physical processor.  If any of those ID's are
	 * already in the table, then kill the fixup.
	 */
	for (id = 0; id < MAXCPU; id++) {
		if ((id_mask & CPUMASK(id)) == 0)
			continue;
		/* First, make sure we are on a logical_cpus boundary. */
		if (id % logical_cpus != 0)
			return 0;
		for (i = id + 1; i < id + logical_cpus; i++)
			if ((id_mask & CPUMASK(i)) != 0)
				return 0;
	}
	return logical_cpus;
}

static int
mptable_map(struct mptable_pos *mpt)
{
	mpfps_t fps = NULL;
	mpcth_t cth = NULL;
	vm_size_t cth_mapsz = 0;

	KKASSERT(mptable_fps_phyaddr != 0);

	bzero(mpt, sizeof(*mpt));

	fps = pmap_mapdev(mptable_fps_phyaddr, sizeof(*fps));
	if (fps->pap != 0) {
		/*
		 * Map configuration table header to get
		 * the base table size
		 */
		cth = pmap_mapdev(fps->pap, sizeof(*cth));
		cth_mapsz = cth->base_table_length;
		pmap_unmapdev((vm_offset_t)cth, sizeof(*cth));

		if (cth_mapsz < sizeof(*cth)) {
			kprintf("invalid base MP table length %d\n",
				(int)cth_mapsz);
			pmap_unmapdev((vm_offset_t)fps, sizeof(*fps));
			return EINVAL;
		}

		/*
		 * Map the base table
		 */
		cth = pmap_mapdev(fps->pap, cth_mapsz);
	}

	mpt->mp_fps = fps;
	mpt->mp_cth = cth;
	mpt->mp_cth_mapsz = cth_mapsz;

	return 0;
}

static void
mptable_unmap(struct mptable_pos *mpt)
{
	if (mpt->mp_cth != NULL) {
		pmap_unmapdev((vm_offset_t)mpt->mp_cth, mpt->mp_cth_mapsz);
		mpt->mp_cth = NULL;
		mpt->mp_cth_mapsz = 0;
	}
	if (mpt->mp_fps != NULL) {
		pmap_unmapdev((vm_offset_t)mpt->mp_fps, sizeof(*mpt->mp_fps));
		mpt->mp_fps = NULL;
	}
}

void
mp_set_cpuids(int cpu_id, int apic_id)
{
	CPU_TO_ID(cpu_id) = apic_id;
	ID_TO_CPU(apic_id) = cpu_id;
}

static int
processor_entry(const struct PROCENTRY *entry, int cpu)
{
	KKASSERT(cpu > 0);

	/* check for usability */
	if (!(entry->cpu_flags & PROCENTRY_FLAG_EN))
		return 0;

	/* check for BSP flag */
	if (entry->cpu_flags & PROCENTRY_FLAG_BP) {
		mp_set_cpuids(0, entry->apic_id);
		return 0;	/* its already been counted */
	}

	/* add another AP to list, if less than max number of CPUs */
	else if (cpu < MAXCPU) {
		mp_set_cpuids(cpu, entry->apic_id);
		return 1;
	}

	return 0;
}

/*
 * Map a physical memory address representing I/O into KVA.  The I/O
 * block is assumed not to cross a page boundary.
 */
void *
ioapic_map(vm_paddr_t pa)
{
	KKASSERT(pa < 0x100000000LL);

	return pmap_mapdev_uncacheable(pa, PAGE_SIZE);
}

/*
 * start each AP in our list
 */
static int
start_all_aps(u_int boot_addr)
{
	vm_offset_t va = boot_address + KERNBASE;
	u_int64_t *pt4, *pt3, *pt2;
	int     x, i, pg;
	int	shift;
	int	smicount;
	int	smibest;
	int	smilast;
	u_char  mpbiosreason;
	u_long  mpbioswarmvec;
	struct mdglobaldata *gd;
	struct privatespace *ps;

	POSTCODE(START_ALL_APS_POST);

	/* install the AP 1st level boot code */
	pmap_kenter(va, boot_address);
	cpu_invlpg((void *)va);		/* JG XXX */
	bcopy(mptramp_start, (void *)va, bootMP_size);

	/* Locate the page tables, they'll be below the trampoline */
	pt4 = (u_int64_t *)(uintptr_t)(mptramp_pagetables + KERNBASE);
	pt3 = pt4 + (PAGE_SIZE) / sizeof(u_int64_t);
	pt2 = pt3 + (PAGE_SIZE) / sizeof(u_int64_t);

	/* Create the initial 1GB replicated page tables */
	for (i = 0; i < 512; i++) {
		/* Each slot of the level 4 pages points to the same level 3 page */
		pt4[i] = (u_int64_t)(uintptr_t)(mptramp_pagetables + PAGE_SIZE);
		pt4[i] |= PG_V | PG_RW | PG_U;

		/* Each slot of the level 3 pages points to the same level 2 page */
		pt3[i] = (u_int64_t)(uintptr_t)(mptramp_pagetables + (2 * PAGE_SIZE));
		pt3[i] |= PG_V | PG_RW | PG_U;

		/* The level 2 page slots are mapped with 2MB pages for 1GB. */
		pt2[i] = i * (2 * 1024 * 1024);
		pt2[i] |= PG_V | PG_RW | PG_PS | PG_U;
	}

	/* save the current value of the warm-start vector */
	mpbioswarmvec = *((u_int32_t *) WARMBOOT_OFF);
	outb(CMOS_REG, BIOS_RESET);
	mpbiosreason = inb(CMOS_DATA);

	/* setup a vector to our boot code */
	*((volatile u_short *) WARMBOOT_OFF) = WARMBOOT_TARGET;
	*((volatile u_short *) WARMBOOT_SEG) = (boot_address >> 4);
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

	/* start each AP */
	for (x = 1; x <= mp_naps; ++x) {

		/* This is a bit verbose, it will go away soon.  */

		/* first page of AP's private space */
		pg = x * x86_64_btop(sizeof(struct privatespace));

		/* allocate new private data page(s) */
		gd = (struct mdglobaldata *)kmem_alloc(&kernel_map, 
				MDGLOBALDATA_BASEALLOC_SIZE);

		gd = &CPU_prvspace[x].mdglobaldata;	/* official location */
		bzero(gd, sizeof(*gd));
		gd->mi.gd_prvspace = ps = &CPU_prvspace[x];

		/* prime data page for it to use */
		mi_gdinit(&gd->mi, x);
		cpu_gdinit(gd, x);
		gd->mi.gd_ipiq = (void *)kmem_alloc(&kernel_map, sizeof(lwkt_ipiq) * (mp_naps + 1));
		bzero(gd->mi.gd_ipiq, sizeof(lwkt_ipiq) * (mp_naps + 1));

		/* setup a vector to our boot code */
		*((volatile u_short *) WARMBOOT_OFF) = WARMBOOT_TARGET;
		*((volatile u_short *) WARMBOOT_SEG) = (boot_addr >> 4);
		outb(CMOS_REG, BIOS_RESET);
		outb(CMOS_DATA, BIOS_WARM);	/* 'warm-start' */

		/*
		 * Setup the AP boot stack
		 */
		bootSTK = &ps->idlestack[UPAGES*PAGE_SIZE/2];
		bootAP = x;

		/* attempt to start the Application Processor */
		CHECK_INIT(99);	/* setup checkpoints */
		if (!start_ap(gd, boot_addr, smibest)) {
			kprintf("\nAP #%d (PHY# %d) failed!\n",
				x, CPU_TO_ID(x));
			CHECK_PRINT("trace");	/* show checkpoints */
			/* better panic as the AP may be running loose */
			kprintf("panic y/n? [y] ");
			if (cngetc() != 'n')
				panic("bye-bye");
		}
		CHECK_PRINT("trace");		/* show checkpoints */

		/* record its version info */
		cpu_apic_versions[x] = cpu_apic_versions[0];
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

	/* fill in our (BSP) APIC version */
	cpu_apic_versions[0] = lapic->version;

	/* restore the warmstart vector */
	*(u_long *) WARMBOOT_OFF = mpbioswarmvec;
	outb(CMOS_REG, BIOS_RESET);
	outb(CMOS_DATA, mpbiosreason);

	/*
	 * NOTE!  The idlestack for the BSP was setup by locore.  Finish
	 * up, clean out the P==V mapping we did earlier.
	 */
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

#if 0

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
	*dst16 = (u_int) boot_addr & 0xffff;
	*dst8 = ((u_int) boot_addr >> 16) & 0xff;

	/* modify the target for boot data segment */
	dst16 = (u_int16_t *) (dst + ((u_int) bootDataSeg - boot_base));
	dst8 = (u_int8_t *) (dst16 + 1);
	*dst16 = (u_int) boot_addr & 0xffff;
	*dst8 = ((u_int) boot_addr >> 16) & 0xff;
}

#endif

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
	physical_cpu = CPU_TO_ID(gd->mi.gd_cpuid);

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
	 *	 ldelta a little but ndelta will be so huge when the SMI
	 *	 occurs the detection logic will still work fine.
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
	lapic->icr_lo = icr_lo | 0x00004500;
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
 * Synchronously flush the TLB on all other CPU's.  The current cpu's
 * TLB is not flushed.  If the caller wishes to flush the current cpu's
 * TLB the caller must call cpu_invltlb() in addition to smp_invltlb().
 *
 * NOTE: If for some reason we were unable to start all cpus we cannot
 *	 safely use broadcast IPIs.
 */

static cpumask_t smp_invltlb_req;

#define SMP_INVLTLB_DEBUG

void
smp_invltlb(void)
{
#ifdef SMP
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
				"rflags %016jx retry",
			      (long)md->gd_invltlb_ret,
			      (long)smp_invltlb_req,
			      (intmax_t)read_rflags());
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
				Debugger("giving up");
			count = 0;
			goto again;
		}
#endif
	}
	atomic_clear_cpumask(&smp_invltlb_req, md->mi.gd_cpumask);
	crit_exit_gd(&md->mi);
#endif
}

#ifdef SMP

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

	cpu_mfence();
	mask = smp_invltlb_req;
	cpu_invltlb();
	while (mask) {
		cpu = BSFCPUMASK(mask);
		mask &= ~CPUMASK(cpu);
		omd = (struct mdglobaldata *)globaldata_find(cpu);
		atomic_set_cpumask(&omd->gd_invltlb_ret, md->mi.gd_cpumask);
	}
}

#endif

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
	u_int	apic_id;

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

	/* Build our map of 'other' CPUs. */
	mycpu->gd_other_cpus = smp_startup_mask & ~CPUMASK(mycpu->gd_cpuid);

	/* A quick check from sanity claus */
	apic_id = (apic_id_to_logical[(lapic->id & 0xff000000) >> 24]);
	if (mycpu->gd_cpuid != apic_id) {
		kprintf("SMP: cpuid = %d\n", mycpu->gd_cpuid);
		kprintf("SMP: apic_id = %d lapicid %d\n",
			apic_id, (lapic->id & 0xff000000) >> 24);
#if JGXXX
		kprintf("PTD[MPPTDI] = %p\n", (void *)PTD[MPPTDI]);
#endif
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
	if (bootverbose) {
		kprintf("Active CPU Mask: %016jx\n",
			(uintmax_t)smp_active_mask);
	}
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

static int
mptable_bus_info_callback(void *xarg, const void *pos, int type)
{
	struct mptable_bus_info *bus_info = xarg;
	const struct BUSENTRY *ent;
	struct mptable_bus *bus;

	if (type != 1)
		return 0;

	ent = pos;
	TAILQ_FOREACH(bus, &bus_info->mbi_list, mb_link) {
		if (bus->mb_id == ent->bus_id) {
			kprintf("mptable_bus_info_alloc: duplicated bus id "
				"(%d)\n", bus->mb_id);
			return EINVAL;
		}
	}

	bus = NULL;
	if (strncmp(ent->bus_type, "PCI", 3) == 0) {
		bus = kmalloc(sizeof(*bus), M_TEMP, M_WAITOK | M_ZERO);
		bus->mb_type = MPTABLE_BUS_PCI;
	} else if (strncmp(ent->bus_type, "ISA", 3) == 0) {
		bus = kmalloc(sizeof(*bus), M_TEMP, M_WAITOK | M_ZERO);
		bus->mb_type = MPTABLE_BUS_ISA;
	}

	if (bus != NULL) {
		bus->mb_id = ent->bus_id;
		TAILQ_INSERT_TAIL(&bus_info->mbi_list, bus, mb_link);
	}
	return 0;
}

static void
mptable_bus_info_alloc(const mpcth_t cth, struct mptable_bus_info *bus_info)
{
	int error;

	bzero(bus_info, sizeof(*bus_info));
	TAILQ_INIT(&bus_info->mbi_list);

	error = mptable_iterate_entries(cth, mptable_bus_info_callback, bus_info);
	if (error)
		mptable_bus_info_free(bus_info);
}

static void
mptable_bus_info_free(struct mptable_bus_info *bus_info)
{
	struct mptable_bus *bus;

	while ((bus = TAILQ_FIRST(&bus_info->mbi_list)) != NULL) {
		TAILQ_REMOVE(&bus_info->mbi_list, bus, mb_link);
		kfree(bus, M_TEMP);
	}
}

struct mptable_lapic_cbarg1 {
	int	cpu_count;
	int	ht_fixup;
	u_int	ht_apicid_mask;
};

static int
mptable_lapic_pass1_callback(void *xarg, const void *pos, int type)
{
	const struct PROCENTRY *ent;
	struct mptable_lapic_cbarg1 *arg = xarg;

	if (type != 0)
		return 0;
	ent = pos;

	if ((ent->cpu_flags & PROCENTRY_FLAG_EN) == 0)
		return 0;

	arg->cpu_count++;
	if (ent->apic_id < 32) {
		arg->ht_apicid_mask |= 1 << ent->apic_id;
	} else if (arg->ht_fixup) {
		kprintf("MPTABLE: lapic id > 32, disable HTT fixup\n");
		arg->ht_fixup = 0;
	}
	return 0;
}

struct mptable_lapic_cbarg2 {
	int	cpu;
	int	logical_cpus;
	int	found_bsp;
};

static int
mptable_lapic_pass2_callback(void *xarg, const void *pos, int type)
{
	const struct PROCENTRY *ent;
	struct mptable_lapic_cbarg2 *arg = xarg;

	if (type != 0)
		return 0;
	ent = pos;

	if (ent->cpu_flags & PROCENTRY_FLAG_BP) {
		KKASSERT(!arg->found_bsp);
		arg->found_bsp = 1;
	}

	if (processor_entry(ent, arg->cpu))
		arg->cpu++;

	if (arg->logical_cpus) {
		struct PROCENTRY proc;
		int i;

		/*
		 * Create fake mptable processor entries
		 * and feed them to processor_entry() to
		 * enumerate the logical CPUs.
		 */
		bzero(&proc, sizeof(proc));
		proc.type = 0;
		proc.cpu_flags = PROCENTRY_FLAG_EN;
		proc.apic_id = ent->apic_id;

		for (i = 1; i < arg->logical_cpus; i++) {
			proc.apic_id++;
			processor_entry(&proc, arg->cpu);
			arg->cpu++;
		}
	}
	return 0;
}

static void
mptable_lapic_default(void)
{
	int ap_apicid, bsp_apicid;

	mp_naps = 1; /* exclude BSP */

	/* Map local apic before the id field is accessed */
	lapic_map(DEFAULT_APIC_BASE);

	bsp_apicid = APIC_ID(lapic->id);
	ap_apicid = (bsp_apicid == 0) ? 1 : 0;

	/* BSP */
	mp_set_cpuids(0, bsp_apicid);
	/* one and only AP */
	mp_set_cpuids(1, ap_apicid);
}

/*
 * Configure:
 *     mp_naps
 *     ID_TO_CPU(N), APIC ID to logical CPU table
 *     CPU_TO_ID(N), logical CPU to APIC ID table
 */
static void
mptable_lapic_enumerate(struct lapic_enumerator *e)
{
	struct mptable_pos mpt;
	struct mptable_lapic_cbarg1 arg1;
	struct mptable_lapic_cbarg2 arg2;
	mpcth_t cth;
	int error, logical_cpus = 0;
	vm_offset_t lapic_addr;

	if (mptable_use_default) {
		mptable_lapic_default();
		return;
	}
 
	error = mptable_map(&mpt);
	if (error)
		panic("mptable_lapic_enumerate mptable_map failed\n");
	KKASSERT(!MPTABLE_POS_USE_DEFAULT(&mpt));

	cth = mpt.mp_cth;
 
	/* Save local apic address */
	lapic_addr = (vm_offset_t)cth->apic_address;
	KKASSERT(lapic_addr != 0);
 
	/*
	 * Find out how many CPUs do we have
	 */
	bzero(&arg1, sizeof(arg1));
	arg1.ht_fixup = 1; /* Apply ht fixup by default */

	error = mptable_iterate_entries(cth,
		    mptable_lapic_pass1_callback, &arg1);
	if (error)
		panic("mptable_iterate_entries(lapic_pass1) failed\n");
	KKASSERT(arg1.cpu_count != 0);
 
	/* See if we need to fixup HT logical CPUs. */
	if (arg1.ht_fixup) {
		logical_cpus = mptable_hyperthread_fixup(arg1.ht_apicid_mask,
							 arg1.cpu_count);
		if (logical_cpus != 0)
			arg1.cpu_count *= logical_cpus;
	}
	mp_naps = arg1.cpu_count;
 
	/* Qualify the numbers again, after possible HT fixup */
	if (mp_naps > MAXCPU) {
		kprintf("Warning: only using %d of %d available CPUs!\n",
			MAXCPU, mp_naps);
		DELAY(1000000);
		mp_naps = MAXCPU;
 	}

	--mp_naps;	/* subtract the BSP */

	/*
	 * Link logical CPU id to local apic id
	 */
	bzero(&arg2, sizeof(arg2));
	arg2.cpu = 1;
	arg2.logical_cpus = logical_cpus;

	error = mptable_iterate_entries(cth,
		    mptable_lapic_pass2_callback, &arg2);
	if (error)
		panic("mptable_iterate_entries(lapic_pass2) failed\n");
	KKASSERT(arg2.found_bsp);

	/* Map local apic */
	lapic_map(lapic_addr);

	mptable_unmap(&mpt);
}

struct mptable_lapic_probe_cbarg {
	int	cpu_count;
	int	found_bsp;
};

static int
mptable_lapic_probe_callback(void *xarg, const void *pos, int type)
{
	const struct PROCENTRY *ent;
	struct mptable_lapic_probe_cbarg *arg = xarg;

	if (type != 0)
		return 0;
	ent = pos;

	if ((ent->cpu_flags & PROCENTRY_FLAG_EN) == 0)
		return 0;
	arg->cpu_count++;

	if (ent->cpu_flags & PROCENTRY_FLAG_BP) {
		if (arg->found_bsp) {
			kprintf("more than one BSP in base MP table\n");
			return EINVAL;
		}
		arg->found_bsp = 1;
	}
	return 0;
}

static int
mptable_lapic_probe(struct lapic_enumerator *e)
{
	struct mptable_pos mpt;
	struct mptable_lapic_probe_cbarg arg;
	mpcth_t cth;
	int error;

	if (mptable_fps_phyaddr == 0)
		return ENXIO;

	if (mptable_use_default)
		return 0;

	error = mptable_map(&mpt);
	if (error)
		return error;
	KKASSERT(!MPTABLE_POS_USE_DEFAULT(&mpt));

	error = EINVAL;
	cth = mpt.mp_cth;

	if (cth->apic_address == 0)
		goto done;

	bzero(&arg, sizeof(arg));
	error = mptable_iterate_entries(cth,
		    mptable_lapic_probe_callback, &arg);
	if (!error) {
		if (arg.cpu_count == 0) {
			kprintf("MP table contains no processor entries\n");
			error = EINVAL;
		} else if (!arg.found_bsp) {
			kprintf("MP table does not contains BSP entry\n");
			error = EINVAL;
		}
	}
done:
	mptable_unmap(&mpt);
	return error;
}

static struct lapic_enumerator	mptable_lapic_enumerator = {
	.lapic_prio = LAPIC_ENUM_PRIO_MPTABLE,
	.lapic_probe = mptable_lapic_probe,
	.lapic_enumerate = mptable_lapic_enumerate
};

static void
mptable_lapic_enum_register(void)
{
	lapic_enumerator_register(&mptable_lapic_enumerator);
}
SYSINIT(mptable_lapic, SI_BOOT2_PRESMP, SI_ORDER_ANY,
	mptable_lapic_enum_register, 0);

static int
mptable_ioapic_list_callback(void *xarg, const void *pos, int type)
{
	const struct IOAPICENTRY *ent;
	struct mptable_ioapic *nioapic, *ioapic;

	if (type != 2)
		return 0;
	ent = pos;

	if ((ent->apic_flags & IOAPICENTRY_FLAG_EN) == 0)
		return 0;

	if (ent->apic_address == 0) {
		kprintf("mptable_ioapic_create_list: zero IOAPIC addr\n");
		return EINVAL;
	}

	TAILQ_FOREACH(ioapic, &mptable_ioapic_list, mio_link) {
		if (ioapic->mio_apic_id == ent->apic_id) {
			kprintf("mptable_ioapic_create_list: duplicated "
				"apic id %d\n", ioapic->mio_apic_id);
			return EINVAL;
		}
		if (ioapic->mio_addr == ent->apic_address) {
			kprintf("mptable_ioapic_create_list: overlapped "
				"IOAPIC addr 0x%08x", ioapic->mio_addr);
			return EINVAL;
		}
	}

	nioapic = kmalloc(sizeof(*nioapic), M_DEVBUF, M_WAITOK | M_ZERO);
	nioapic->mio_apic_id = ent->apic_id;
	nioapic->mio_addr = ent->apic_address;

	/*
	 * Create IOAPIC list in ascending order of APIC ID
	 */
	TAILQ_FOREACH_REVERSE(ioapic, &mptable_ioapic_list,
	    mptable_ioapic_list, mio_link) {
		if (nioapic->mio_apic_id > ioapic->mio_apic_id) {
			TAILQ_INSERT_AFTER(&mptable_ioapic_list,
			    ioapic, nioapic, mio_link);
			break;
		}
	}
	if (ioapic == NULL)
		TAILQ_INSERT_HEAD(&mptable_ioapic_list, nioapic, mio_link);

	return 0;
}

static void
mptable_ioapic_create_list(void)
{
	struct mptable_ioapic *ioapic;
	struct mptable_pos mpt;
	int idx, error;

	if (mptable_fps_phyaddr == 0)
		return;

	if (mptable_use_default) {
		ioapic = kmalloc(sizeof(*ioapic), M_DEVBUF, M_WAITOK | M_ZERO);
		ioapic->mio_idx = 0;
		ioapic->mio_apic_id = 0;	/* NOTE: any value is ok here */
		ioapic->mio_addr = 0xfec00000;	/* XXX magic number */

		TAILQ_INSERT_HEAD(&mptable_ioapic_list, ioapic, mio_link);
		return;
	}

	error = mptable_map(&mpt);
	if (error)
		panic("mptable_ioapic_create_list: mptable_map failed\n");
	KKASSERT(!MPTABLE_POS_USE_DEFAULT(&mpt));

	error = mptable_iterate_entries(mpt.mp_cth,
		    mptable_ioapic_list_callback, NULL);
	if (error) {
		while ((ioapic = TAILQ_FIRST(&mptable_ioapic_list)) != NULL) {
			TAILQ_REMOVE(&mptable_ioapic_list, ioapic, mio_link);
			kfree(ioapic, M_DEVBUF);
		}
		goto done;
	}

	/*
	 * Assign index number for each IOAPIC
	 */
	idx = 0;
	TAILQ_FOREACH(ioapic, &mptable_ioapic_list, mio_link) {
		ioapic->mio_idx = idx;
		++idx;
	}
done:
	mptable_unmap(&mpt);
}
SYSINIT(mptable_ioapic_list, SI_BOOT2_PRESMP, SI_ORDER_SECOND,
	mptable_ioapic_create_list, 0);

static int
mptable_pci_int_callback(void *xarg, const void *pos, int type)
{
	const struct mptable_bus_info *bus_info = xarg;
	const struct mptable_ioapic *ioapic;
	const struct mptable_bus *bus;
	struct mptable_pci_int *pci_int;
	const struct INTENTRY *ent;
	int pci_pin, pci_dev;

	if (type != 3)
		return 0;
	ent = pos;

	if (ent->int_type != 0)
		return 0;

	TAILQ_FOREACH(bus, &bus_info->mbi_list, mb_link) {
		if (bus->mb_type == MPTABLE_BUS_PCI &&
		    bus->mb_id == ent->src_bus_id)
			break;
	}
	if (bus == NULL)
		return 0;

	TAILQ_FOREACH(ioapic, &mptable_ioapic_list, mio_link) {
		if (ioapic->mio_apic_id == ent->dst_apic_id)
			break;
	}
	if (ioapic == NULL) {
		kprintf("MPTABLE: warning PCI int dst apic id %d "
			"does not exist\n", ent->dst_apic_id);
		return 0;
	}

	pci_pin = ent->src_bus_irq & 0x3;
	pci_dev = (ent->src_bus_irq >> 2) & 0x1f;

	TAILQ_FOREACH(pci_int, &mptable_pci_int_list, mpci_link) {
		if (pci_int->mpci_bus == ent->src_bus_id &&
		    pci_int->mpci_dev == pci_dev &&
		    pci_int->mpci_pin == pci_pin) {
			if (pci_int->mpci_ioapic_idx == ioapic->mio_idx &&
			    pci_int->mpci_ioapic_pin == ent->dst_apic_int) {
				kprintf("MPTABLE: warning duplicated "
					"PCI int entry for "
					"bus %d, dev %d, pin %d\n",
					pci_int->mpci_bus,
					pci_int->mpci_dev,
					pci_int->mpci_pin);
				return 0;
			} else {
				kprintf("mptable_pci_int_register: "
					"conflict PCI int entry for "
					"bus %d, dev %d, pin %d, "
					"IOAPIC %d.%d -> %d.%d\n",
					pci_int->mpci_bus,
					pci_int->mpci_dev,
					pci_int->mpci_pin,
					pci_int->mpci_ioapic_idx,
					pci_int->mpci_ioapic_pin,
					ioapic->mio_idx,
					ent->dst_apic_int);
				return EINVAL;
			}
		}
	}

	pci_int = kmalloc(sizeof(*pci_int), M_DEVBUF, M_WAITOK | M_ZERO);

	pci_int->mpci_bus = ent->src_bus_id;
	pci_int->mpci_dev = pci_dev;
	pci_int->mpci_pin = pci_pin;
	pci_int->mpci_ioapic_idx = ioapic->mio_idx;
	pci_int->mpci_ioapic_pin = ent->dst_apic_int;

	TAILQ_INSERT_TAIL(&mptable_pci_int_list, pci_int, mpci_link);

	return 0;
}

static void
mptable_pci_int_register(void)
{
	struct mptable_bus_info bus_info;
	const struct mptable_bus *bus;
	struct mptable_pci_int *pci_int;
	struct mptable_pos mpt;
	int error, force_pci0, npcibus;
	mpcth_t cth;

	if (mptable_fps_phyaddr == 0)
		return;

	if (mptable_use_default)
		return;

	if (TAILQ_EMPTY(&mptable_ioapic_list))
		return;

	error = mptable_map(&mpt);
	if (error)
		panic("mptable_pci_int_register: mptable_map failed\n");
	KKASSERT(!MPTABLE_POS_USE_DEFAULT(&mpt));

	cth = mpt.mp_cth;

	mptable_bus_info_alloc(cth, &bus_info);
	if (TAILQ_EMPTY(&bus_info.mbi_list))
		goto done;

	force_pci0 = 0;
	npcibus = 0;
	TAILQ_FOREACH(bus, &bus_info.mbi_list, mb_link) {
		if (bus->mb_type == MPTABLE_BUS_PCI)
			++npcibus;
	}
	if (npcibus == 0) {
		mptable_bus_info_free(&bus_info);
		goto done;
	} else if (npcibus == 1) {
		force_pci0 = 1;
	}

	error = mptable_iterate_entries(cth,
		    mptable_pci_int_callback, &bus_info);

	mptable_bus_info_free(&bus_info);

	if (error) {
		while ((pci_int = TAILQ_FIRST(&mptable_pci_int_list)) != NULL) {
			TAILQ_REMOVE(&mptable_pci_int_list, pci_int, mpci_link);
			kfree(pci_int, M_DEVBUF);
		}
		goto done;
	}

	if (force_pci0) {
		TAILQ_FOREACH(pci_int, &mptable_pci_int_list, mpci_link)
			pci_int->mpci_bus = 0;
	}
done:
	mptable_unmap(&mpt);
}
SYSINIT(mptable_pci, SI_BOOT2_PRESMP, SI_ORDER_ANY,
	mptable_pci_int_register, 0);

struct mptable_ioapic_probe_cbarg {
	const struct mptable_bus_info *bus_info;
};

static int
mptable_ioapic_probe_callback(void *xarg, const void *pos, int type)
{
	struct mptable_ioapic_probe_cbarg *arg = xarg;
	const struct mptable_ioapic *ioapic;
	const struct mptable_bus *bus;
	const struct INTENTRY *ent;

	if (type != 3)
		return 0;
	ent = pos;

	if (ent->int_type != 0)
		return 0;

	TAILQ_FOREACH(bus, &arg->bus_info->mbi_list, mb_link) {
		if (bus->mb_type == MPTABLE_BUS_ISA &&
		    bus->mb_id == ent->src_bus_id)
			break;
	}
	if (bus == NULL)
		return 0;

	TAILQ_FOREACH(ioapic, &mptable_ioapic_list, mio_link) {
		if (ioapic->mio_apic_id == ent->dst_apic_id)
			break;
	}
	if (ioapic == NULL) {
		kprintf("MPTABLE: warning ISA int dst apic id %d "
			"does not exist\n", ent->dst_apic_id);
		return 0;
	}

	/* XXX magic number */
	if (ent->src_bus_irq >= 16) {
		kprintf("mptable_ioapic_probe: invalid ISA irq (%d)\n",
			ent->src_bus_irq);
		return EINVAL;
	}
	return 0;
}

static int
mptable_ioapic_probe(struct ioapic_enumerator *e)
{
	struct mptable_ioapic_probe_cbarg arg;
	struct mptable_bus_info bus_info;
	struct mptable_pos mpt;
	mpcth_t cth;
	int error;

	if (mptable_fps_phyaddr == 0)
		return ENXIO;

	if (mptable_use_default)
		return 0;

	if (TAILQ_EMPTY(&mptable_ioapic_list))
		return ENXIO;

	error = mptable_map(&mpt);
	if (error)
		panic("mptable_ioapic_probe: mptable_map failed\n");
	KKASSERT(!MPTABLE_POS_USE_DEFAULT(&mpt));

	cth = mpt.mp_cth;

	mptable_bus_info_alloc(cth, &bus_info);

	bzero(&arg, sizeof(arg));
	arg.bus_info = &bus_info;

	error = mptable_iterate_entries(cth,
		    mptable_ioapic_probe_callback, &arg);

	mptable_bus_info_free(&bus_info);
	mptable_unmap(&mpt);

	return error;
}

struct mptable_ioapic_int_cbarg {
	const struct mptable_bus_info *bus_info;
	int	ioapic_nint;
};

static int
mptable_ioapic_int_callback(void *xarg, const void *pos, int type)
{
	struct mptable_ioapic_int_cbarg *arg = xarg;
	const struct mptable_ioapic *ioapic;
	const struct mptable_bus *bus;
	const struct INTENTRY *ent;
	int gsi;

	if (type != 3)
		return 0;

	arg->ioapic_nint++;

	ent = pos;
	if (ent->int_type != 0)
		return 0;

	TAILQ_FOREACH(bus, &arg->bus_info->mbi_list, mb_link) {
		if (bus->mb_type == MPTABLE_BUS_ISA &&
		    bus->mb_id == ent->src_bus_id)
			break;
	}
	if (bus == NULL)
		return 0;

	TAILQ_FOREACH(ioapic, &mptable_ioapic_list, mio_link) {
		if (ioapic->mio_apic_id == ent->dst_apic_id)
			break;
	}
	if (ioapic == NULL) {
		kprintf("MPTABLE: warning ISA int dst apic id %d "
			"does not exist\n", ent->dst_apic_id);
		return 0;
	}

	if (ent->dst_apic_int >= ioapic->mio_npin) {
		panic("mptable_ioapic_enumerate: invalid I/O APIC "
		      "pin %d, should be < %d",
		      ent->dst_apic_int, ioapic->mio_npin);
	}
	gsi = ioapic->mio_gsi_base + ent->dst_apic_int;

	if (ent->src_bus_irq != gsi) {
		if (bootverbose) {
			kprintf("MPTABLE: INTSRC irq %d -> GSI %d\n",
				ent->src_bus_irq, gsi);
		}
		ioapic_intsrc(ent->src_bus_irq, gsi,
		    INTR_TRIGGER_EDGE, INTR_POLARITY_HIGH);
	}
	return 0;
}

static void
mptable_ioapic_enumerate(struct ioapic_enumerator *e)
{
	struct mptable_bus_info bus_info;
	struct mptable_ioapic *ioapic;
	struct mptable_pos mpt;
	mpcth_t cth;
	int error;

	KKASSERT(mptable_fps_phyaddr != 0);
	KKASSERT(!TAILQ_EMPTY(&mptable_ioapic_list));

	TAILQ_FOREACH(ioapic, &mptable_ioapic_list, mio_link) {
		const struct mptable_ioapic *prev_ioapic;
		uint32_t ver;
		void *addr;

		addr = ioapic_map(ioapic->mio_addr);

		ver = ioapic_read(addr, IOAPIC_VER);
		ioapic->mio_npin = ((ver & IOART_VER_MAXREDIR)
				    >> MAXREDIRSHIFT) + 1;

		prev_ioapic = TAILQ_PREV(ioapic,
				mptable_ioapic_list, mio_link);
		if (prev_ioapic == NULL) {
			ioapic->mio_gsi_base = 0;
		} else {
			ioapic->mio_gsi_base =
				prev_ioapic->mio_gsi_base +
				prev_ioapic->mio_npin;
		}
		ioapic_add(addr, ioapic->mio_gsi_base, ioapic->mio_npin);

		if (bootverbose) {
			kprintf("MPTABLE: IOAPIC addr 0x%08x, "
				"apic id %d, idx %d, gsi base %d, npin %d\n",
				ioapic->mio_addr,
				ioapic->mio_apic_id,
				ioapic->mio_idx,
				ioapic->mio_gsi_base,
				ioapic->mio_npin);
		}
	}

	if (mptable_use_default) {
		if (bootverbose)
			kprintf("MPTABLE: INTSRC irq 0 -> GSI 2 (default)\n");
		ioapic_intsrc(0, 2, INTR_TRIGGER_EDGE, INTR_POLARITY_HIGH);
		return;
	}

	error = mptable_map(&mpt);
	if (error)
		panic("mptable_ioapic_probe: mptable_map failed\n");
	KKASSERT(!MPTABLE_POS_USE_DEFAULT(&mpt));

	cth = mpt.mp_cth;

	mptable_bus_info_alloc(cth, &bus_info);

	if (TAILQ_EMPTY(&bus_info.mbi_list)) {
		if (bootverbose)
			kprintf("MPTABLE: INTSRC irq 0 -> GSI 2 (no bus)\n");
		ioapic_intsrc(0, 2, INTR_TRIGGER_EDGE, INTR_POLARITY_HIGH);
	} else {
		struct mptable_ioapic_int_cbarg arg;

		bzero(&arg, sizeof(arg));
		arg.bus_info = &bus_info;

		error = mptable_iterate_entries(cth,
			    mptable_ioapic_int_callback, &arg);
		if (error)
			panic("mptable_ioapic_int failed\n");

		if (arg.ioapic_nint == 0) {
			if (bootverbose) {
				kprintf("MPTABLE: INTSRC irq 0 -> GSI 2 "
					"(no int)\n");
			}
			ioapic_intsrc(0, 2, INTR_TRIGGER_EDGE,
			    INTR_POLARITY_HIGH);
		}
	}

	mptable_bus_info_free(&bus_info);

	mptable_unmap(&mpt);
}

static struct ioapic_enumerator	mptable_ioapic_enumerator = {
	.ioapic_prio = IOAPIC_ENUM_PRIO_MPTABLE,
	.ioapic_probe = mptable_ioapic_probe,
	.ioapic_enumerate = mptable_ioapic_enumerate
};

static void
mptable_ioapic_enum_register(void)
{
	ioapic_enumerator_register(&mptable_ioapic_enumerator);
}
SYSINIT(mptable_ioapic, SI_BOOT2_PRESMP, SI_ORDER_ANY,
	mptable_ioapic_enum_register, 0);

void
mptable_pci_int_dump(void)
{
	const struct mptable_pci_int *pci_int;

	TAILQ_FOREACH(pci_int, &mptable_pci_int_list, mpci_link) {
		kprintf("MPTABLE: %d:%d INT%c -> IOAPIC %d.%d\n",
			pci_int->mpci_bus,
			pci_int->mpci_dev,
			pci_int->mpci_pin + 'A',
			pci_int->mpci_ioapic_idx,
			pci_int->mpci_ioapic_pin);
	}
}

int
mptable_pci_int_route(int bus, int dev, int pin, int intline)
{
	const struct mptable_pci_int *pci_int;
	int irq = -1;

	KKASSERT(pin >= 1);
	--pin;	/* zero based */

	TAILQ_FOREACH(pci_int, &mptable_pci_int_list, mpci_link) {
		if (pci_int->mpci_bus == bus &&
		    pci_int->mpci_dev == dev &&
		    pci_int->mpci_pin == pin)
			break;
	}
	if (pci_int != NULL) {
		int gsi;

		gsi = ioapic_gsi(pci_int->mpci_ioapic_idx,
			pci_int->mpci_ioapic_pin);
		if (gsi >= 0) {
			irq = ioapic_abi_find_gsi(gsi,
				INTR_TRIGGER_LEVEL, INTR_POLARITY_LOW);
		}
	}

	if (irq < 0) {
		if (bootverbose) {
			kprintf("MPTABLE: fixed interrupt routing "
				"for %d:%d INT%c\n", bus, dev, pin + 'A');
		}

		irq = ioapic_abi_find_irq(intline,
			INTR_TRIGGER_LEVEL, INTR_POLARITY_LOW);
	}

	if (irq >= 0 && bootverbose) {
		kprintf("MPTABLE: %d:%d INT%c routed to irq %d\n",
			bus, dev, pin + 'A', irq);
	}
	return irq;
}
