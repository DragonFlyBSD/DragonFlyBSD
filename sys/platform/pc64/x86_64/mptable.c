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
#include <machine_base/isa/isa_intr.h>
#include <machine_base/apic/apicreg.h>
#include <machine_base/apic/apicvar.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine_base/apic/lapic.h>
#include <machine_base/apic/ioapic.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/tss.h>
#include <machine/specialreg.h>
#include <machine/globaldata.h>
#include <machine/pmap_inval.h>
#include <machine/mptable.h>

#include <machine/md_var.h>		/* setidt() */
#include <machine_base/icu/icu.h>	/* IPIs */
#include <machine_base/apic/ioapic_abi.h>
#include <machine/intr_machdep.h>	/* IPIs */

extern u_int	base_memory;
extern u_long	ebda_addr;
extern int	imcr_present;
extern int	naps;

static int	force_enable = 0;
TUNABLE_INT("hw.lapic_force_enable", &force_enable);

#define BIOS_BASE		(0xf0000)
#define BIOS_BASE2		(0xe0000)
#define BIOS_SIZE		(0x10000)
#define BIOS_COUNT		(BIOS_SIZE/4)

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
static int	mptable_lapic_enumerate(struct lapic_enumerator *);
static void	mptable_lapic_default(void);

static int	mptable_ioapic_probe(struct ioapic_enumerator *);
static void	mptable_ioapic_enumerate(struct ioapic_enumerator *);

static basetable_entry basetable_entry_types[] =
{
	{0, 20, "Processor"},
	{1, 8, "Bus"},
	{2, 8, "I/O APIC"},
	{3, 8, "I/O INT"},
	{4, 8, "Local INT"}
};

static vm_paddr_t	mptable_fps_phyaddr;
static int		mptable_use_default;
static TAILQ_HEAD(mptable_pci_int_list, mptable_pci_int) mptable_pci_int_list =
	TAILQ_HEAD_INITIALIZER(mptable_pci_int_list);
static TAILQ_HEAD(mptable_ioapic_list, mptable_ioapic) mptable_ioapic_list =
	TAILQ_HEAD_INITIALIZER(mptable_ioapic_list);

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

	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
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
			if (CPUMASK_TESTBIT(id_mask, id) == 0)
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
		if (CPUMASK_TESTBIT(id_mask, id) == 0)
			continue;
		/* First, make sure we are on a logical_cpus boundary. */
		if (id % logical_cpus != 0)
			return 0;
		for (i = id + 1; i < id + logical_cpus; i++)
			if (CPUMASK_TESTBIT(id_mask, i) != 0)
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

static int
processor_entry(const struct PROCENTRY *entry, int cpu)
{
	KKASSERT(cpu > 0);

	/* check for usability */
	if (!(entry->cpu_flags & PROCENTRY_FLAG_EN))
		return 0;

	/* check for BSP flag */
	if (entry->cpu_flags & PROCENTRY_FLAG_BP) {
		lapic_set_cpuid(0, entry->apic_id);
		return 0;	/* its already been counted */
	}

	/* add another AP to list, if less than max number of CPUs */
	else if (cpu < MAXCPU) {
		lapic_set_cpuid(cpu, entry->apic_id);
		return 1;
	}

	return 0;
}

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
	if (ent->apic_id < 64) {
		arg->ht_apicid_mask |= 1UL << ent->apic_id;
	} else if (arg->ht_fixup) {
		kprintf("MPTABLE: lapic id > 64, disable HTT fixup\n");
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
		proc.cpu_flags = (force_enable) ? PROCENTRY_FLAG_EN : ent->cpu_flags;
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

	naps = 1; /* exclude BSP */

	/* Map local apic before the id field is accessed */
	lapic_map(DEFAULT_APIC_BASE);

	bsp_apicid = APIC_ID(lapic->id);
	ap_apicid = (bsp_apicid == 0) ? 1 : 0;

	/* BSP */
	lapic_set_cpuid(0, bsp_apicid);
	/* one and only AP */
	lapic_set_cpuid(1, ap_apicid);
}

/*
 * Configure:
 *     naps
 *     APIC ID <-> CPU ID mappings
 */
static int
mptable_lapic_enumerate(struct lapic_enumerator *e)
{
	struct mptable_pos mpt;
	struct mptable_lapic_cbarg1 arg1;
	struct mptable_lapic_cbarg2 arg2;
	mpcth_t cth;
	int error, logical_cpus = 0;
	vm_paddr_t lapic_addr;

	if (mptable_use_default) {
		mptable_lapic_default();
		return 0;
	}
 
	error = mptable_map(&mpt);
	if (error)
		panic("mptable_lapic_enumerate mptable_map failed");
	KKASSERT(!MPTABLE_POS_USE_DEFAULT(&mpt));

	cth = mpt.mp_cth;
 
	/* Save local apic address */
	lapic_addr = cth->apic_address;
	KKASSERT(lapic_addr != 0);
 
	/*
	 * Find out how many CPUs do we have
	 */
	bzero(&arg1, sizeof(arg1));
	arg1.ht_fixup = 1; /* Apply ht fixup by default */

	error = mptable_iterate_entries(cth,
		    mptable_lapic_pass1_callback, &arg1);
	if (error)
		panic("mptable_iterate_entries(lapic_pass1) failed");
	KKASSERT(arg1.cpu_count != 0);
 
	/* See if we need to fixup HT logical CPUs. */
	/*
	 * XXX fixup for cpus >= 32 ? XXX
	 */
	if (arg1.ht_fixup) {
		cpumask_t mask;

		CPUMASK_ASSZERO(mask);
		mask.m0 = arg1.ht_apicid_mask;
		logical_cpus = mptable_hyperthread_fixup(mask, arg1.cpu_count);
		if (logical_cpus != 0)
			arg1.cpu_count *= logical_cpus;
	}
	naps = arg1.cpu_count - 1;	/* subtract the BSP */
 
	/*
	 * Link logical CPU id to local apic id
	 */
	bzero(&arg2, sizeof(arg2));
	arg2.cpu = 1;
	arg2.logical_cpus = logical_cpus;

	error = mptable_iterate_entries(cth,
		    mptable_lapic_pass2_callback, &arg2);
	if (error)
		panic("mptable_iterate_entries(lapic_pass2) failed");
	KKASSERT(arg2.found_bsp);

	/* Map local apic */
	lapic_map(lapic_addr);

	mptable_unmap(&mpt);

	return 0;
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

	if (ent->apic_id == APICID_MAX) {
		kprintf("MPTABLE: invalid LAPIC apic id %d\n",
		    ent->apic_id);
		return EINVAL;
	}

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
	if (ent->apic_id == APICID_MAX) {
		kprintf("mptable_ioapic_create_list: "
		    "invalid IOAPIC apic id %d\n", ent->apic_id);
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
		panic("mptable_ioapic_create_list: mptable_map failed");
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
		panic("mptable_pci_int_register: mptable_map failed");
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
	if (ent->src_bus_irq >= ISA_IRQ_CNT) {
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
		panic("mptable_ioapic_probe: mptable_map failed");
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
		panic("mptable_ioapic_probe: mptable_map failed");
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
			panic("mptable_ioapic_int failed");

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

	if (!bootverbose)
		return;

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
			irq = machintr_legacy_intr_find_bygsi(gsi,
				INTR_TRIGGER_LEVEL, INTR_POLARITY_LOW);
		}
	}

	if (irq < 0 && intline >= 0) {
		kprintf("MPTABLE: fixed interrupt routing "
		    "for %d:%d INT%c\n", bus, dev, pin + 'A');
		irq = machintr_legacy_intr_find(intline,
			INTR_TRIGGER_LEVEL, INTR_POLARITY_LOW);
	}

	if (irq >= 0 && bootverbose) {
		kprintf("MPTABLE: %d:%d INT%c routed to irq %d\n",
			bus, dev, pin + 'A', irq);
	}
	return irq;
}
