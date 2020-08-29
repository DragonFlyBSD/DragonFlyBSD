/*-
 * Copyright (c) 1982, 1987, 1990 The Regents of the University of California.
 * Copyright (c) 1992 Terrence R. Lambert.
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 2008-2017 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: @(#)machdep.c	7.4 (Berkeley) 6/3/91
 * $FreeBSD: src/sys/i386/i386/machdep.c,v 1.385.2.30 2003/05/31 08:48:05 alc Exp $
 */

//#include "use_npx.h"
#include "use_isa.h"
#include "opt_cpu.h"
#include "opt_ddb.h"
#include "opt_inet.h"
#include "opt_msgbuf.h"
#include "opt_swap.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmsg.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/mbuf.h>
#include <sys/msgbuf.h>
#include <sys/sysent.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <sys/bus.h>
#include <sys/usched.h>
#include <sys/reg.h>
#include <sys/sbuf.h>
#include <sys/ctype.h>
#include <sys/serialize.h>
#include <sys/systimer.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>

#include <sys/exec.h>
#include <sys/cons.h>

#include <sys/efi.h>

#include <ddb/ddb.h>

#include <machine/cpu.h>
#include <machine/clock.h>
#include <machine/specialreg.h>
#if 0 /* JG */
#include <machine/bootinfo.h>
#endif
#include <machine/md_var.h>
#include <machine/metadata.h>
#include <machine/pc/bios.h>
#include <machine/pcb_ext.h>
#include <machine/globaldata.h>		/* CPU_prvspace */
#include <machine/smp.h>
#include <machine/cputypes.h>
#include <machine/intr_machdep.h>
#include <machine/framebuffer.h>

#ifdef OLD_BUS_ARCH
#include <bus/isa/isa_device.h>
#endif
#include <machine_base/isa/isa_intr.h>
#include <bus/isa/rtc.h>
#include <sys/random.h>
#include <sys/ptrace.h>
#include <machine/sigframe.h>

#include <sys/machintr.h>
#include <machine_base/icu/icu_abi.h>
#include <machine_base/icu/elcr_var.h>
#include <machine_base/apic/lapic.h>
#include <machine_base/apic/ioapic.h>
#include <machine_base/apic/ioapic_abi.h>
#include <machine/mptable.h>

#define PHYSMAP_ENTRIES		10
#define MAXBUFSTRUCTSIZE	((size_t)512 * 1024 * 1024)

extern u_int64_t hammer_time(u_int64_t, u_int64_t);

extern void printcpuinfo(void);	/* XXX header file */
extern void identify_cpu(void);
extern void panicifcpuunsupported(void);

static void cpu_startup(void *);
static void pic_finish(void *);
static void cpu_finish(void *);

static void set_fpregs_xmm(struct save87 *, struct savexmm *);
static void fill_fpregs_xmm(struct savexmm *, struct save87 *);
static void init_locks(void);

extern void pcpu_timer_always(struct intrframe *);

SYSINIT(cpu, SI_BOOT2_START_CPU, SI_ORDER_FIRST, cpu_startup, NULL);
SYSINIT(pic_finish, SI_BOOT2_FINISH_PIC, SI_ORDER_FIRST, pic_finish, NULL);
SYSINIT(cpu_finish, SI_BOOT2_FINISH_CPU, SI_ORDER_FIRST, cpu_finish, NULL);

#ifdef DDB
extern vm_offset_t ksym_start, ksym_end;
#endif

struct privatespace CPU_prvspace_bsp __aligned(4096);
struct privatespace *CPU_prvspace[MAXCPU] = { &CPU_prvspace_bsp };

vm_paddr_t efi_systbl_phys;
int	_udatasel, _ucodesel, _ucode32sel;
u_long	atdevbase;
int64_t tsc_offsets[MAXCPU];
cpumask_t smp_idleinvl_mask;
cpumask_t smp_idleinvl_reqs;

 /* MWAIT hint (EAX) or CPU_MWAIT_HINT_ */
__read_mostly static int cpu_mwait_halt_global;
__read_mostly static int clock_debug1;
__read_mostly static int flame_poll_debug;

SYSCTL_INT(_debug, OID_AUTO, flame_poll_debug,
	CTLFLAG_RW, &flame_poll_debug, 0, "");
TUNABLE_INT("debug.flame_poll_debug", &flame_poll_debug);

#if defined(SWTCH_OPTIM_STATS)
extern int swtch_optim_stats;
SYSCTL_INT(_debug, OID_AUTO, swtch_optim_stats,
	CTLFLAG_RD, &swtch_optim_stats, 0, "");
SYSCTL_INT(_debug, OID_AUTO, tlb_flush_count,
	CTLFLAG_RD, &tlb_flush_count, 0, "");
#endif
SYSCTL_INT(_debug, OID_AUTO, clock_debug1,
	CTLFLAG_RW, &clock_debug1, 0, "");
SYSCTL_INT(_hw, OID_AUTO, cpu_mwait_halt,
	CTLFLAG_RD, &cpu_mwait_halt_global, 0, "");
SYSCTL_INT(_hw, OID_AUTO, cpu_mwait_spin,
	CTLFLAG_RD, &cpu_mwait_spin, 0, "monitor/mwait target state");

#define CPU_MWAIT_HAS_CX	\
	((cpu_feature2 & CPUID2_MON) && \
	 (cpu_mwait_feature & CPUID_MWAIT_EXT))

#define CPU_MWAIT_CX_NAMELEN	16

#define CPU_MWAIT_C1		1
#define CPU_MWAIT_C2		2
#define CPU_MWAIT_C3		3
#define CPU_MWAIT_CX_MAX	8

#define CPU_MWAIT_HINT_AUTO	-1	/* C1 and C2 */
#define CPU_MWAIT_HINT_AUTODEEP	-2	/* C3+ */

SYSCTL_NODE(_machdep, OID_AUTO, mwait, CTLFLAG_RW, 0, "MWAIT features");
SYSCTL_NODE(_machdep_mwait, OID_AUTO, CX, CTLFLAG_RW, 0, "MWAIT Cx settings");

struct cpu_mwait_cx {
	int			subcnt;
	char			name[4];
	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
};
static struct cpu_mwait_cx	cpu_mwait_cx_info[CPU_MWAIT_CX_MAX];
static char			cpu_mwait_cx_supported[256];

static int			cpu_mwait_c1_hints_cnt;
static int			cpu_mwait_hints_cnt;
static int			*cpu_mwait_hints;

static int			cpu_mwait_deep_hints_cnt;
static int			*cpu_mwait_deep_hints;

#define CPU_IDLE_REPEAT_DEFAULT	750

static u_int			cpu_idle_repeat = CPU_IDLE_REPEAT_DEFAULT;
static u_long			cpu_idle_repeat_max = CPU_IDLE_REPEAT_DEFAULT;
static u_int			cpu_mwait_repeat_shift = 1;

#define CPU_MWAIT_C3_PREAMBLE_BM_ARB	0x1
#define CPU_MWAIT_C3_PREAMBLE_BM_STS	0x2

static int			cpu_mwait_c3_preamble =
				    CPU_MWAIT_C3_PREAMBLE_BM_ARB |
				    CPU_MWAIT_C3_PREAMBLE_BM_STS;

SYSCTL_STRING(_machdep_mwait_CX, OID_AUTO, supported, CTLFLAG_RD,
    cpu_mwait_cx_supported, 0, "MWAIT supported C states");
SYSCTL_INT(_machdep_mwait_CX, OID_AUTO, c3_preamble, CTLFLAG_RD,
    &cpu_mwait_c3_preamble, 0, "C3+ preamble mask");

static int	cpu_mwait_cx_select_sysctl(SYSCTL_HANDLER_ARGS,
		    int *, boolean_t);
static int	cpu_mwait_cx_idle_sysctl(SYSCTL_HANDLER_ARGS);
static int	cpu_mwait_cx_pcpu_idle_sysctl(SYSCTL_HANDLER_ARGS);
static int	cpu_mwait_cx_spin_sysctl(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_machdep_mwait_CX, OID_AUTO, idle, CTLTYPE_STRING|CTLFLAG_RW,
    NULL, 0, cpu_mwait_cx_idle_sysctl, "A", "");
SYSCTL_PROC(_machdep_mwait_CX, OID_AUTO, spin, CTLTYPE_STRING|CTLFLAG_RW,
    NULL, 0, cpu_mwait_cx_spin_sysctl, "A", "");
SYSCTL_UINT(_machdep_mwait_CX, OID_AUTO, repeat_shift, CTLFLAG_RW,
    &cpu_mwait_repeat_shift, 0, "");

long physmem = 0;

u_long ebda_addr = 0;

int imcr_present = 0;

int naps = 0; /* # of Applications processors */

u_int base_memory;

static int
sysctl_hw_physmem(SYSCTL_HANDLER_ARGS)
{
	u_long pmem = ctob(physmem);
	int error;

	error = sysctl_handle_long(oidp, &pmem, 0, req);

	return (error);
}

SYSCTL_PROC(_hw, HW_PHYSMEM, physmem, CTLTYPE_ULONG|CTLFLAG_RD,
	0, 0, sysctl_hw_physmem, "LU",
	"Total system memory in bytes (number of pages * page size)");

static int
sysctl_hw_usermem(SYSCTL_HANDLER_ARGS)
{
	u_long usermem = ctob(physmem - vmstats.v_wire_count);
	int error;

	error = sysctl_handle_long(oidp, &usermem, 0, req);

	return (error);
}

SYSCTL_PROC(_hw, HW_USERMEM, usermem, CTLTYPE_ULONG|CTLFLAG_RD,
	0, 0, sysctl_hw_usermem, "LU", "");

static int
sysctl_hw_availpages(SYSCTL_HANDLER_ARGS)
{
	int error;
	u_long availpages;

	availpages = x86_64_btop(avail_end - avail_start);
	error = sysctl_handle_long(oidp, &availpages, 0, req);

	return (error);
}

SYSCTL_PROC(_hw, OID_AUTO, availpages, CTLTYPE_ULONG|CTLFLAG_RD,
	0, 0, sysctl_hw_availpages, "LU", "");

vm_paddr_t Maxmem;
vm_paddr_t Realmem;

/*
 * The number of PHYSMAP entries must be one less than the number of
 * PHYSSEG entries because the PHYSMAP entry that spans the largest
 * physical address that is accessible by ISA DMA is split into two
 * PHYSSEG entries.
 */
vm_phystable_t phys_avail[VM_PHYSSEG_MAX + 1];
vm_phystable_t dump_avail[VM_PHYSSEG_MAX + 1];

/* must be 1 less so 0 0 can signal end of chunks */
#define PHYS_AVAIL_ARRAY_END (NELEM(phys_avail) - 1)
#define DUMP_AVAIL_ARRAY_END (NELEM(dump_avail) - 1)

static vm_offset_t buffer_sva, buffer_eva;
vm_offset_t clean_sva, clean_eva;
static vm_offset_t pager_sva, pager_eva;
static struct trapframe proc0_tf;

static void cpu_implement_smap(void);

static void
cpu_startup(void *dummy)
{
	caddr_t v;
	vm_size_t size = 0;
	vm_offset_t firstaddr;

	/*
	 * Good {morning,afternoon,evening,night}.
	 */
	kprintf("%s", version);
	startrtclock();
	printcpuinfo();
	panicifcpuunsupported();
	if (cpu_stdext_feature & CPUID_STDEXT_SMAP)
		cpu_implement_smap();

	kprintf("real memory  = %ju (%ju MB)\n",
		(intmax_t)Realmem,
		(intmax_t)Realmem / 1024 / 1024);
	/*
	 * Display any holes after the first chunk of extended memory.
	 */
	if (bootverbose) {
		int indx;

		kprintf("Physical memory chunk(s):\n");
		for (indx = 0; phys_avail[indx].phys_end != 0; ++indx) {
			vm_paddr_t size1;

			size1 = phys_avail[indx].phys_end -
				phys_avail[indx].phys_beg;

			kprintf("0x%08jx - 0x%08jx, %ju bytes (%ju pages)\n",
				(intmax_t)phys_avail[indx].phys_beg,
				(intmax_t)phys_avail[indx].phys_end - 1,
				(intmax_t)size1,
				(intmax_t)(size1 / PAGE_SIZE));
		}
	}

	/*
	 * Allocate space for system data structures.
	 * The first available kernel virtual address is in "v".
	 * As pages of kernel virtual memory are allocated, "v" is incremented.
	 * As pages of memory are allocated and cleared,
	 * "firstaddr" is incremented.
	 * An index into the kernel page table corresponding to the
	 * virtual memory address maintained in "v" is kept in "mapaddr".
	 */

	/*
	 * Make two passes.  The first pass calculates how much memory is
	 * needed and allocates it.  The second pass assigns virtual
	 * addresses to the various data structures.
	 */
	firstaddr = 0;
again:
	v = (caddr_t)firstaddr;

#define	valloc(name, type, num) \
	    (name) = (type *)v; v = (caddr_t)((name)+(num))
#define	valloclim(name, type, num, lim) \
	    (name) = (type *)v; v = (caddr_t)((lim) = ((name)+(num)))

	/*
	 * Calculate nbuf such that maxbufspace uses approximately 1/20
	 * of physical memory by default, with a minimum of 50 buffers.
	 *
	 * The calculation is made after discounting 128MB.
	 *
	 * NOTE: maxbufspace is (nbuf * NBUFCALCSIZE) (NBUFCALCSIZE ~= 16KB).
	 *	 nbuf = (kbytes / factor) would cover all of memory.
	 */
	if (nbuf == 0) {
		long factor = NBUFCALCSIZE / 1024;		/* KB/nbuf */
		long kbytes = physmem * (PAGE_SIZE / 1024);	/* physmem */

		nbuf = 50;
		if (kbytes > 128 * 1024)
			nbuf += (kbytes - 128 * 1024) / (factor * 20);
		if (maxbcache && nbuf > maxbcache / NBUFCALCSIZE)
			nbuf = maxbcache / NBUFCALCSIZE;
		if ((size_t)nbuf * sizeof(struct buf) > MAXBUFSTRUCTSIZE) {
			kprintf("Warning: nbuf capped at %ld due to the "
				"reasonability limit\n", nbuf);
			nbuf = MAXBUFSTRUCTSIZE / sizeof(struct buf);
		}
	}

	/*
	 * Do not allow the buffer_map to be more then 1/2 the size of the
	 * kernel_map.
	 */
	if (nbuf > (virtual_end - virtual_start +
		    virtual2_end - virtual2_start) / (MAXBSIZE * 2)) {
		nbuf = (virtual_end - virtual_start +
			virtual2_end - virtual2_start) / (MAXBSIZE * 2);
		kprintf("Warning: nbufs capped at %ld due to kvm\n", nbuf);
	}

	/*
	 * Do not allow the buffer_map to use more than 50% of available
	 * physical-equivalent memory.  Since the VM pages which back
	 * individual buffers are typically wired, having too many bufs
	 * can prevent the system from paging properly.
	 */
	if (nbuf > physmem * PAGE_SIZE / (NBUFCALCSIZE * 2)) {
		nbuf = physmem * PAGE_SIZE / (NBUFCALCSIZE * 2);
		kprintf("Warning: nbufs capped at %ld due to physmem\n", nbuf);
	}

	/*
	 * Do not allow the sizeof(struct buf) * nbuf to exceed 1/4 of
	 * the valloc space which is just the virtual_end - virtual_start
	 * section.  This is typically ~2GB regardless of the amount of
	 * memory, so we use 500MB as a metric.
	 *
	 * This is because we use valloc() to allocate the buf header array.
	 *
	 * NOTE: buffer space in bytes is limited by vfs.*bufspace sysctls.
	 */
	if (nbuf > (virtual_end - virtual_start) / (sizeof(struct buf) * 4)) {
		nbuf = (virtual_end - virtual_start) /
		       (sizeof(struct buf) * 4);
		kprintf("Warning: nbufs capped at %ld due to "
			"valloc considerations\n",
			nbuf);
	}

	nswbuf_mem = lmax(lmin(nbuf / 32, 512), 8);
#ifdef NSWBUF_MIN
	if (nswbuf_mem < NSWBUF_MIN)
		nswbuf_mem = NSWBUF_MIN;
#endif
	nswbuf_kva = lmax(lmin(nbuf / 4, 512), 16);
#ifdef NSWBUF_MIN
	if (nswbuf_kva < NSWBUF_MIN)
		nswbuf_kva = NSWBUF_MIN;
#endif

	valloc(swbuf_mem, struct buf, nswbuf_mem);
	valloc(swbuf_kva, struct buf, nswbuf_kva);
	valloc(buf, struct buf, nbuf);

	/*
	 * End of first pass, size has been calculated so allocate memory
	 */
	if (firstaddr == 0) {
		size = (vm_size_t)(v - firstaddr);
		firstaddr = kmem_alloc(&kernel_map, round_page(size),
				       VM_SUBSYS_BUF);
		if (firstaddr == 0)
			panic("startup: no room for tables");
		goto again;
	}

	/*
	 * End of second pass, addresses have been assigned
	 *
	 * nbuf is an int, make sure we don't overflow the field.
	 *
	 * On 64-bit systems we always reserve maximal allocations for
	 * buffer cache buffers and there are no fragmentation issues,
	 * so the KVA segment does not have to be excessively oversized.
	 */
	if ((vm_size_t)(v - firstaddr) != size)
		panic("startup: table size inconsistency");

	kmem_suballoc(&kernel_map, &clean_map, &clean_sva, &clean_eva,
		      ((vm_offset_t)(nbuf + 16) * MAXBSIZE) +
		      ((nswbuf_mem + nswbuf_kva) * MAXPHYS) + pager_map_size);
	kmem_suballoc(&clean_map, &buffer_map, &buffer_sva, &buffer_eva,
		      ((vm_offset_t)(nbuf + 16) * MAXBSIZE));
	buffer_map.system_map = 1;
	kmem_suballoc(&clean_map, &pager_map, &pager_sva, &pager_eva,
		      ((vm_offset_t)(nswbuf_mem + nswbuf_kva) * MAXPHYS) +
		      pager_map_size);
	pager_map.system_map = 1;
	kprintf("avail memory = %ju (%ju MB)\n",
		(uintmax_t)ptoa(vmstats.v_free_count + vmstats.v_dma_pages),
		(uintmax_t)ptoa(vmstats.v_free_count + vmstats.v_dma_pages) /
		1024 / 1024);
}

struct cpu_idle_stat {
	int	hint;
	int	reserved;
	u_long	halt;
	u_long	spin;
	u_long	repeat;
	u_long	repeat_last;
	u_long	repeat_delta;
	u_long	mwait_cx[CPU_MWAIT_CX_MAX];
} __cachealign;

#define CPU_IDLE_STAT_HALT	-1
#define CPU_IDLE_STAT_SPIN	-2

static struct cpu_idle_stat	cpu_idle_stats[MAXCPU];

static int
sysctl_cpu_idle_cnt(SYSCTL_HANDLER_ARGS)
{
	int idx = arg2, cpu, error;
	u_long val = 0;

	if (idx == CPU_IDLE_STAT_HALT) {
		for (cpu = 0; cpu < ncpus; ++cpu)
			val += cpu_idle_stats[cpu].halt;
	} else if (idx == CPU_IDLE_STAT_SPIN) {
		for (cpu = 0; cpu < ncpus; ++cpu)
			val += cpu_idle_stats[cpu].spin;
	} else {
		KASSERT(idx >= 0 && idx < CPU_MWAIT_CX_MAX,
		    ("invalid index %d", idx));
		for (cpu = 0; cpu < ncpus; ++cpu)
			val += cpu_idle_stats[cpu].mwait_cx[idx];
	}

	error = sysctl_handle_quad(oidp, &val, 0, req);
        if (error || req->newptr == NULL)
	        return error;

	if (idx == CPU_IDLE_STAT_HALT) {
		for (cpu = 0; cpu < ncpus; ++cpu)
			cpu_idle_stats[cpu].halt = 0;
		cpu_idle_stats[0].halt = val;
	} else if (idx == CPU_IDLE_STAT_SPIN) {
		for (cpu = 0; cpu < ncpus; ++cpu)
			cpu_idle_stats[cpu].spin = 0;
		cpu_idle_stats[0].spin = val;
	} else {
		KASSERT(idx >= 0 && idx < CPU_MWAIT_CX_MAX,
		    ("invalid index %d", idx));
		for (cpu = 0; cpu < ncpus; ++cpu)
			cpu_idle_stats[cpu].mwait_cx[idx] = 0;
		cpu_idle_stats[0].mwait_cx[idx] = val;
	}
	return 0;
}

static void
cpu_mwait_attach(void)
{
	struct sbuf sb;
	int hint_idx, i;

	if (!CPU_MWAIT_HAS_CX)
		return;

	if (cpu_vendor_id == CPU_VENDOR_INTEL &&
	    (CPUID_TO_FAMILY(cpu_id) > 0xf ||
	     (CPUID_TO_FAMILY(cpu_id) == 0x6 &&
	      CPUID_TO_MODEL(cpu_id) >= 0xf))) {
		int bm_sts = 1;

		/*
		 * Pentium dual-core, Core 2 and beyond do not need any
		 * additional activities to enter deep C-state, i.e. C3(+).
		 */
		cpu_mwait_cx_no_bmarb();

		TUNABLE_INT_FETCH("machdep.cpu.mwait.bm_sts", &bm_sts);
		if (!bm_sts)
			cpu_mwait_cx_no_bmsts();
	}

	sbuf_new(&sb, cpu_mwait_cx_supported,
	    sizeof(cpu_mwait_cx_supported), SBUF_FIXEDLEN);

	for (i = 0; i < CPU_MWAIT_CX_MAX; ++i) {
		struct cpu_mwait_cx *cx = &cpu_mwait_cx_info[i];
		int sub;

		ksnprintf(cx->name, sizeof(cx->name), "C%d", i);

		sysctl_ctx_init(&cx->sysctl_ctx);
		cx->sysctl_tree = SYSCTL_ADD_NODE(&cx->sysctl_ctx,
		    SYSCTL_STATIC_CHILDREN(_machdep_mwait), OID_AUTO,
		    cx->name, CTLFLAG_RW, NULL, "Cx control/info");
		if (cx->sysctl_tree == NULL)
			continue;

		cx->subcnt = CPUID_MWAIT_CX_SUBCNT(cpu_mwait_extemu, i);
		SYSCTL_ADD_INT(&cx->sysctl_ctx,
		    SYSCTL_CHILDREN(cx->sysctl_tree), OID_AUTO,
		    "subcnt", CTLFLAG_RD, &cx->subcnt, 0,
		    "sub-state count");
		SYSCTL_ADD_PROC(&cx->sysctl_ctx,
		    SYSCTL_CHILDREN(cx->sysctl_tree), OID_AUTO,
		    "entered", (CTLTYPE_QUAD | CTLFLAG_RW), 0,
		    i, sysctl_cpu_idle_cnt, "Q", "# of times entered");

		for (sub = 0; sub < cx->subcnt; ++sub)
			sbuf_printf(&sb, "C%d/%d ", i, sub);
	}
	sbuf_trim(&sb);
	sbuf_finish(&sb);

	/*
	 * Non-deep C-states
	 */
	cpu_mwait_c1_hints_cnt = cpu_mwait_cx_info[CPU_MWAIT_C1].subcnt;
	for (i = CPU_MWAIT_C1; i < CPU_MWAIT_C3; ++i)
		cpu_mwait_hints_cnt += cpu_mwait_cx_info[i].subcnt;
	cpu_mwait_hints = kmalloc(sizeof(int) * cpu_mwait_hints_cnt,
				  M_DEVBUF, M_WAITOK);

	hint_idx = 0;
	for (i = CPU_MWAIT_C1; i < CPU_MWAIT_C3; ++i) {
		int j, subcnt;

		subcnt = cpu_mwait_cx_info[i].subcnt;
		for (j = 0; j < subcnt; ++j) {
			KASSERT(hint_idx < cpu_mwait_hints_cnt,
			    ("invalid mwait hint index %d", hint_idx));
			cpu_mwait_hints[hint_idx] = MWAIT_EAX_HINT(i, j);
			++hint_idx;
		}
	}
	KASSERT(hint_idx == cpu_mwait_hints_cnt,
	    ("mwait hint count %d != index %d",
	     cpu_mwait_hints_cnt, hint_idx));

	if (bootverbose) {
		kprintf("MWAIT hints (%d C1 hints):\n", cpu_mwait_c1_hints_cnt);
		for (i = 0; i < cpu_mwait_hints_cnt; ++i) {
			int hint = cpu_mwait_hints[i];

			kprintf("  C%d/%d hint 0x%04x\n",
			    MWAIT_EAX_TO_CX(hint), MWAIT_EAX_TO_CX_SUB(hint),
			    hint);
		}
	}

	/*
	 * Deep C-states
	 */
	for (i = CPU_MWAIT_C1; i < CPU_MWAIT_CX_MAX; ++i)
		cpu_mwait_deep_hints_cnt += cpu_mwait_cx_info[i].subcnt;
	cpu_mwait_deep_hints = kmalloc(sizeof(int) * cpu_mwait_deep_hints_cnt,
	    M_DEVBUF, M_WAITOK);

	hint_idx = 0;
	for (i = CPU_MWAIT_C1; i < CPU_MWAIT_CX_MAX; ++i) {
		int j, subcnt;

		subcnt = cpu_mwait_cx_info[i].subcnt;
		for (j = 0; j < subcnt; ++j) {
			KASSERT(hint_idx < cpu_mwait_deep_hints_cnt,
			    ("invalid mwait deep hint index %d", hint_idx));
			cpu_mwait_deep_hints[hint_idx] = MWAIT_EAX_HINT(i, j);
			++hint_idx;
		}
	}
	KASSERT(hint_idx == cpu_mwait_deep_hints_cnt,
	    ("mwait deep hint count %d != index %d",
	     cpu_mwait_deep_hints_cnt, hint_idx));

	if (bootverbose) {
		kprintf("MWAIT deep hints:\n");
		for (i = 0; i < cpu_mwait_deep_hints_cnt; ++i) {
			int hint = cpu_mwait_deep_hints[i];

			kprintf("  C%d/%d hint 0x%04x\n",
			    MWAIT_EAX_TO_CX(hint), MWAIT_EAX_TO_CX_SUB(hint),
			    hint);
		}
	}
	cpu_idle_repeat_max = 256 * cpu_mwait_deep_hints_cnt;

	for (i = 0; i < ncpus; ++i) {
		char name[16];

		ksnprintf(name, sizeof(name), "idle%d", i);
		SYSCTL_ADD_PROC(NULL,
		    SYSCTL_STATIC_CHILDREN(_machdep_mwait_CX), OID_AUTO,
		    name, (CTLTYPE_STRING | CTLFLAG_RW), &cpu_idle_stats[i],
		    0, cpu_mwait_cx_pcpu_idle_sysctl, "A", "");
	}
}

static void
cpu_finish(void *dummy __unused)
{
	cpu_setregs();
	cpu_mwait_attach();
}

static void
pic_finish(void *dummy __unused)
{
	/* Log ELCR information */
	elcr_dump();

	/* Log MPTABLE information */
	mptable_pci_int_dump();

	/* Finalize PCI */
	MachIntrABI.finalize();
}

/*
 * Send an interrupt to process.
 *
 * Stack is set up to allow sigcode stored
 * at top to call routine, followed by kcall
 * to sigreturn routine below.  After sigreturn
 * resets the signal mask, the stack, and the
 * frame pointer, it returns to the user
 * specified pc, psl.
 */
void
sendsig(sig_t catcher, int sig, sigset_t *mask, u_long code)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p = lp->lwp_proc;
	struct trapframe *regs;
	struct sigacts *psp = p->p_sigacts;
	struct sigframe sf, *sfp;
	int oonstack;
	char *sp;

	regs = lp->lwp_md.md_regs;
	oonstack = (lp->lwp_sigstk.ss_flags & SS_ONSTACK) ? 1 : 0;

	/* Save user context */
	bzero(&sf, sizeof(struct sigframe));
	sf.sf_uc.uc_sigmask = *mask;
	sf.sf_uc.uc_stack = lp->lwp_sigstk;
	sf.sf_uc.uc_mcontext.mc_onstack = oonstack;
	KKASSERT(__offsetof(struct trapframe, tf_rdi) == 0);
	/* gcc errors out on optimized bcopy */
	_bcopy(regs, &sf.sf_uc.uc_mcontext.mc_rdi, sizeof(struct trapframe));

	/* Make the size of the saved context visible to userland */
	sf.sf_uc.uc_mcontext.mc_len = sizeof(sf.sf_uc.uc_mcontext);

	/* Allocate and validate space for the signal handler context. */
        if ((lp->lwp_flags & LWP_ALTSTACK) != 0 && !oonstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		sp = (char *)lp->lwp_sigstk.ss_sp + lp->lwp_sigstk.ss_size -
		    sizeof(struct sigframe);
		lp->lwp_sigstk.ss_flags |= SS_ONSTACK;
	} else {
		/* We take red zone into account */
		sp = (char *)regs->tf_rsp - sizeof(struct sigframe) - 128;
	}

	/*
	 * XXX AVX needs 64-byte alignment but sigframe has other fields and
	 * the embedded ucontext is not at the front, so aligning this won't
	 * help us.  Fortunately we bcopy in/out of the sigframe, so the
	 * kernel is ok.
	 *
	 * The problem though is if userland winds up trying to use the
	 * context directly.
	 */
	sfp = (struct sigframe *)((intptr_t)sp & ~(intptr_t)0xF);

	/* Translate the signal is appropriate */
	if (p->p_sysent->sv_sigtbl) {
		if (sig <= p->p_sysent->sv_sigsize)
			sig = p->p_sysent->sv_sigtbl[_SIG_IDX(sig)];
	}

	/*
	 * Build the argument list for the signal handler.
	 *
	 * Arguments are in registers (%rdi, %rsi, %rdx, %rcx)
	 */
	regs->tf_rdi = sig;				/* argument 1 */
	regs->tf_rdx = (register_t)&sfp->sf_uc;		/* argument 3 */

	if (SIGISMEMBER(psp->ps_siginfo, sig)) {
		/*
		 * Signal handler installed with SA_SIGINFO.
		 *
		 * action(signo, siginfo, ucontext)
		 */
		regs->tf_rsi = (register_t)&sfp->sf_si;	/* argument 2 */
		regs->tf_rcx = (register_t)regs->tf_addr; /* argument 4 */
		sf.sf_ahu.sf_action = (__siginfohandler_t *)catcher;

		/* fill siginfo structure */
		sf.sf_si.si_signo = sig;
		sf.sf_si.si_pid = psp->ps_frominfo[sig].pid;
		sf.sf_si.si_uid = psp->ps_frominfo[sig].uid;
		sf.sf_si.si_code = code;
		sf.sf_si.si_addr = (void *)regs->tf_addr;
	} else {
		/*
		 * Old FreeBSD-style arguments.
		 *
		 * handler (signo, code, [uc], addr)
		 */
		regs->tf_rsi = (register_t)code;	/* argument 2 */
		regs->tf_rcx = (register_t)regs->tf_addr; /* argument 4 */
		sf.sf_ahu.sf_handler = catcher;
	}

	/*
	 * If we're a vm86 process, we want to save the segment registers.
	 * We also change eflags to be our emulated eflags, not the actual
	 * eflags.
	 */
#if 0 /* JG */
	if (regs->tf_eflags & PSL_VM) {
		struct trapframe_vm86 *tf = (struct trapframe_vm86 *)regs;
		struct vm86_kernel *vm86 = &lp->lwp_thread->td_pcb->pcb_ext->ext_vm86;

		sf.sf_uc.uc_mcontext.mc_gs = tf->tf_vm86_gs;
		sf.sf_uc.uc_mcontext.mc_fs = tf->tf_vm86_fs;
		sf.sf_uc.uc_mcontext.mc_es = tf->tf_vm86_es;
		sf.sf_uc.uc_mcontext.mc_ds = tf->tf_vm86_ds;

		if (vm86->vm86_has_vme == 0)
			sf.sf_uc.uc_mcontext.mc_eflags =
			    (tf->tf_eflags & ~(PSL_VIF | PSL_VIP)) |
			    (vm86->vm86_eflags & (PSL_VIF | PSL_VIP));

		/*
		 * Clear PSL_NT to inhibit T_TSSFLT faults on return from
		 * syscalls made by the signal handler.  This just avoids
		 * wasting time for our lazy fixup of such faults.  PSL_NT
		 * does nothing in vm86 mode, but vm86 programs can set it
		 * almost legitimately in probes for old cpu types.
		 */
		tf->tf_eflags &= ~(PSL_VM | PSL_NT | PSL_VIF | PSL_VIP);
	}
#endif

	/*
	 * Save the FPU state and reinit the FP unit
	 */
	npxpush(&sf.sf_uc.uc_mcontext);

	/*
	 * Copy the sigframe out to the user's stack.
	 */
	if (copyout(&sf, sfp, sizeof(struct sigframe)) != 0) {
		/*
		 * Something is wrong with the stack pointer.
		 * ...Kill the process.
		 */
		sigexit(lp, SIGILL);
	}

	regs->tf_rsp = (register_t)sfp;
	regs->tf_rip = trunc_page64(PS_STRINGS - *(p->p_sysent->sv_szsigcode));
	regs->tf_rip -= SZSIGCODE_EXTRA_BYTES;

	/*
	 * x86 abi specifies that the direction flag must be cleared
	 * on function entry
	 */
	regs->tf_rflags &= ~(PSL_T | PSL_D);

	/*
	 * 64 bit mode has a code and stack selector but
	 * no data or extra selector.  %fs and %gs are not
	 * stored in-context.
	 */
	regs->tf_cs = _ucodesel;
	regs->tf_ss = _udatasel;
	clear_quickret();
}

/*
 * Sanitize the trapframe for a virtual kernel passing control to a custom
 * VM context.  Remove any items that would otherwise create a privilage
 * issue.
 *
 * XXX at the moment we allow userland to set the resume flag.  Is this a
 * bad idea?
 */
int
cpu_sanitize_frame(struct trapframe *frame)
{
	frame->tf_cs = _ucodesel;
	frame->tf_ss = _udatasel;
	/* XXX VM (8086) mode not supported? */
	frame->tf_rflags &= (PSL_RF | PSL_USERCHANGE | PSL_VM_UNSUPP);
	frame->tf_rflags |= PSL_RESERVED_DEFAULT | PSL_I;

	return(0);
}

/*
 * Sanitize the tls so loading the descriptor does not blow up
 * on us.  For x86_64 we don't have to do anything.
 */
int
cpu_sanitize_tls(struct savetls *tls)
{
	return(0);
}

/*
 * sigreturn(ucontext_t *sigcntxp)
 *
 * System call to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by sendsig (above).
 * Return to previous pc and psl as specified by
 * context left by sendsig. Check carefully to
 * make sure that the user has not modified the
 * state to gain improper privileges.
 *
 * MPSAFE
 */
#define	EFL_SECURE(ef, oef)	((((ef) ^ (oef)) & ~PSL_USERCHANGE) == 0)
#define	CS_SECURE(cs)		(ISPL(cs) == SEL_UPL)

int
sys_sigreturn(struct sysmsg *sysmsg, const struct sigreturn_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	struct trapframe *regs;
	ucontext_t uc;
	ucontext_t *ucp;
	register_t rflags;
	int cs;
	int error;

	/*
	 * We have to copy the information into kernel space so userland
	 * can't modify it while we are sniffing it.
	 */
	regs = lp->lwp_md.md_regs;
	error = copyin(uap->sigcntxp, &uc, sizeof(uc));
	if (error)
		return (error);
	ucp = &uc;
	rflags = ucp->uc_mcontext.mc_rflags;

	/* VM (8086) mode not supported */
	rflags &= ~PSL_VM_UNSUPP;

#if 0 /* JG */
	if (eflags & PSL_VM) {
		struct trapframe_vm86 *tf = (struct trapframe_vm86 *)regs;
		struct vm86_kernel *vm86;

		/*
		 * if pcb_ext == 0 or vm86_inited == 0, the user hasn't
		 * set up the vm86 area, and we can't enter vm86 mode.
		 */
		if (lp->lwp_thread->td_pcb->pcb_ext == 0)
			return (EINVAL);
		vm86 = &lp->lwp_thread->td_pcb->pcb_ext->ext_vm86;
		if (vm86->vm86_inited == 0)
			return (EINVAL);

		/* go back to user mode if both flags are set */
		if ((eflags & PSL_VIP) && (eflags & PSL_VIF))
			trapsignal(lp, SIGBUS, 0);

		if (vm86->vm86_has_vme) {
			eflags = (tf->tf_eflags & ~VME_USERCHANGE) |
			    (eflags & VME_USERCHANGE) | PSL_VM;
		} else {
			vm86->vm86_eflags = eflags;	/* save VIF, VIP */
			eflags = (tf->tf_eflags & ~VM_USERCHANGE) |
			    (eflags & VM_USERCHANGE) | PSL_VM;
		}
		bcopy(&ucp->uc_mcontext.mc_gs, tf, sizeof(struct trapframe));
		tf->tf_eflags = eflags;
		tf->tf_vm86_ds = tf->tf_ds;
		tf->tf_vm86_es = tf->tf_es;
		tf->tf_vm86_fs = tf->tf_fs;
		tf->tf_vm86_gs = tf->tf_gs;
		tf->tf_ds = _udatasel;
		tf->tf_es = _udatasel;
		tf->tf_fs = _udatasel;
		tf->tf_gs = _udatasel;
	} else
#endif
	{
		/*
		 * Don't allow users to change privileged or reserved flags.
		 */
		/*
		 * XXX do allow users to change the privileged flag PSL_RF.
		 * The cpu sets PSL_RF in tf_eflags for faults.  Debuggers
		 * should sometimes set it there too.  tf_eflags is kept in
		 * the signal context during signal handling and there is no
		 * other place to remember it, so the PSL_RF bit may be
		 * corrupted by the signal handler without us knowing.
		 * Corruption of the PSL_RF bit at worst causes one more or
		 * one less debugger trap, so allowing it is fairly harmless.
		 */
		if (!EFL_SECURE(rflags & ~PSL_RF, regs->tf_rflags & ~PSL_RF)) {
			kprintf("sigreturn: rflags = 0x%lx\n", (long)rflags);
			return(EINVAL);
		}

		/*
		 * Don't allow users to load a valid privileged %cs.  Let the
		 * hardware check for invalid selectors, excess privilege in
		 * other selectors, invalid %eip's and invalid %esp's.
		 */
		cs = ucp->uc_mcontext.mc_cs;
		if (!CS_SECURE(cs)) {
			kprintf("sigreturn: cs = 0x%x\n", cs);
			trapsignal(lp, SIGBUS, T_PROTFLT);
			return(EINVAL);
		}
		/* gcc errors out on optimized bcopy */
		_bcopy(&ucp->uc_mcontext.mc_rdi, regs,
		       sizeof(struct trapframe));
	}

	/*
	 * Restore the FPU state from the frame
	 */
	crit_enter();
	npxpop(&ucp->uc_mcontext);

	if (ucp->uc_mcontext.mc_onstack & 1)
		lp->lwp_sigstk.ss_flags |= SS_ONSTACK;
	else
		lp->lwp_sigstk.ss_flags &= ~SS_ONSTACK;

	lp->lwp_sigmask = ucp->uc_sigmask;
	SIG_CANTMASK(lp->lwp_sigmask);
	clear_quickret();
	crit_exit();
	return(EJUSTRETURN);
}

/*
 * Machine dependent boot() routine
 *
 * I haven't seen anything to put here yet
 * Possibly some stuff might be grafted back here from boot()
 */
void
cpu_boot(int howto)
{
}

/*
 * Shutdown the CPU as much as possible
 */
void
cpu_halt(void)
{
	for (;;)
		__asm__ __volatile("hlt");
}

/*
 * cpu_idle() represents the idle LWKT.  You cannot return from this function
 * (unless you want to blow things up!).  Instead we look for runnable threads
 * and loop or halt as appropriate.  Giant is not held on entry to the thread.
 *
 * The main loop is entered with a critical section held, we must release
 * the critical section before doing anything else.  lwkt_switch() will
 * check for pending interrupts due to entering and exiting its own
 * critical section.
 *
 * NOTE: On an SMP system we rely on a scheduler IPI to wake a HLTed cpu up.
 *	 However, there are cases where the idlethread will be entered with
 *	 the possibility that no IPI will occur and in such cases
 *	 lwkt_switch() sets TDF_IDLE_NOHLT.
 *
 * NOTE: cpu_idle_repeat determines how many entries into the idle thread
 *	 must occur before it starts using ACPI halt.
 *
 * NOTE: Value overridden in hammer_time().
 */
static int	cpu_idle_hlt = 2;
SYSCTL_INT(_machdep, OID_AUTO, cpu_idle_hlt, CTLFLAG_RW,
    &cpu_idle_hlt, 0, "Idle loop HLT enable");
SYSCTL_INT(_machdep, OID_AUTO, cpu_idle_repeat, CTLFLAG_RW,
    &cpu_idle_repeat, 0, "Idle entries before acpi hlt");

SYSCTL_PROC(_machdep, OID_AUTO, cpu_idle_hltcnt, (CTLTYPE_QUAD | CTLFLAG_RW),
    0, CPU_IDLE_STAT_HALT, sysctl_cpu_idle_cnt, "Q", "Idle loop entry halts");
SYSCTL_PROC(_machdep, OID_AUTO, cpu_idle_spincnt, (CTLTYPE_QUAD | CTLFLAG_RW),
    0, CPU_IDLE_STAT_SPIN, sysctl_cpu_idle_cnt, "Q", "Idle loop entry spins");

static void
cpu_idle_default_hook(void)
{
	/*
	 * We must guarentee that hlt is exactly the instruction
	 * following the sti.
	 */
	__asm __volatile("sti; hlt");
}

/* Other subsystems (e.g., ACPI) can hook this later. */
void (*cpu_idle_hook)(void) = cpu_idle_default_hook;

static __inline int
cpu_mwait_cx_hint(struct cpu_idle_stat *stat)
{
	int hint, cx_idx;
	u_int idx;

	hint = stat->hint;
	if (hint >= 0)
		goto done;

	idx = (stat->repeat + stat->repeat_last + stat->repeat_delta) >>
	    cpu_mwait_repeat_shift;
	if (idx >= cpu_mwait_c1_hints_cnt) {
		/* Step up faster, once we walked through all C1 states */
		stat->repeat_delta += 1 << (cpu_mwait_repeat_shift + 1);
	}
	if (hint == CPU_MWAIT_HINT_AUTODEEP) {
		if (idx >= cpu_mwait_deep_hints_cnt)
			idx = cpu_mwait_deep_hints_cnt - 1;
		hint = cpu_mwait_deep_hints[idx];
	} else {
		if (idx >= cpu_mwait_hints_cnt)
			idx = cpu_mwait_hints_cnt - 1;
		hint = cpu_mwait_hints[idx];
	}
done:
	cx_idx = MWAIT_EAX_TO_CX(hint);
	if (cx_idx >= 0 && cx_idx < CPU_MWAIT_CX_MAX)
		stat->mwait_cx[cx_idx]++;
	return hint;
}

void
cpu_idle(void)
{
	globaldata_t gd = mycpu;
	struct cpu_idle_stat *stat = &cpu_idle_stats[gd->gd_cpuid];
	struct thread *td __debugvar = gd->gd_curthread;
	int reqflags;

	stat->repeat = stat->repeat_last = cpu_idle_repeat_max;

	crit_exit();
	KKASSERT(td->td_critcount == 0);

	for (;;) {
		/*
		 * See if there are any LWKTs ready to go.
		 */
		lwkt_switch();

		/*
		 * When halting inside a cli we must check for reqflags
		 * races, particularly [re]schedule requests.  Running
		 * splz() does the job.
		 *
		 * cpu_idle_hlt:
		 *	0	Never halt, just spin
		 *
		 *	1	Always use MONITOR/MWAIT if avail, HLT
		 *		otherwise.
		 *
		 *		Better default for modern (Haswell+) Intel
		 *		cpus.
		 *
		 *	2	Use HLT/MONITOR/MWAIT up to a point and then
		 *		use the ACPI halt (default).  This is a hybrid
		 *		approach.  See machdep.cpu_idle_repeat.
		 *
		 *		Better default for modern AMD cpus and older
		 *		Intel cpus.
		 *
		 *	3	Always use the ACPI halt.  This typically
		 *		eats the least amount of power but the cpu
		 *		will be slow waking up.  Slows down e.g.
		 *		compiles and other pipe/event oriented stuff.
		 *
		 *		Usually the best default for AMD cpus.
		 *
		 *	4	Always use HLT.
		 *
		 *	5	Always spin.
		 *
		 * NOTE: Interrupts are enabled and we are not in a critical
		 *	 section.
		 *
		 * NOTE: Preemptions do not reset gd_idle_repeat.   Also we
		 *	 don't bother capping gd_idle_repeat, it is ok if
		 *	 it overflows (we do make it unsigned, however).
		 *
		 * Implement optimized invltlb operations when halted
		 * in idle.  By setting the bit in smp_idleinvl_mask
		 * we inform other cpus that they can set _reqs to
		 * request an invltlb.  Current the code to do that
		 * sets the bits in _reqs anyway, but then check _mask
		 * to determine if they can assume the invltlb will execute.
		 *
		 * A critical section is required to ensure that interrupts
		 * do not fully run until after we've had a chance to execute
		 * the request.
		 */
		if (gd->gd_idle_repeat == 0) {
			stat->repeat = (stat->repeat + stat->repeat_last) >> 1;
			if (stat->repeat > cpu_idle_repeat_max)
				stat->repeat = cpu_idle_repeat_max;
			stat->repeat_last = 0;
			stat->repeat_delta = 0;
		}
		++stat->repeat_last;

		/*
		 * General idle thread halt code
		 *
		 * IBRS NOTES - IBRS is a SPECTRE mitigation.  When going
		 *		idle, disable IBRS to reduce hyperthread
		 *		overhead.
		 */
		++gd->gd_idle_repeat;

		switch(cpu_idle_hlt) {
		default:
		case 0:
			/*
			 * Always spin
			 */
			;
do_spin:
			splz();
			__asm __volatile("sti");
			stat->spin++;
			crit_enter_gd(gd);
			crit_exit_gd(gd);
			break;
		case 2:
			/*
			 * Use MONITOR/MWAIT (or HLT) for a few cycles,
			 * then start using the ACPI halt code if we
			 * continue to be idle.
			 */
			if (gd->gd_idle_repeat >= cpu_idle_repeat)
				goto do_acpi;
			/* FALL THROUGH */
		case 1:
			/*
			 * Always use MONITOR/MWAIT (will use HLT if
			 * MONITOR/MWAIT not available).
			 */
			if (cpu_mi_feature & CPU_MI_MONITOR) {
				splz(); /* XXX */
				reqflags = gd->gd_reqflags;
				if (reqflags & RQF_IDLECHECK_WK_MASK)
					goto do_spin;
				crit_enter_gd(gd);
				ATOMIC_CPUMASK_ORBIT(smp_idleinvl_mask, gd->gd_cpuid);
				/*
				 * IBRS/STIBP
				 */
				if (pscpu->trampoline.tr_pcb_spec_ctrl[1] &
				    SPEC_CTRL_DUMMY_ENABLE) {
					wrmsr(MSR_SPEC_CTRL, pscpu->trampoline.tr_pcb_spec_ctrl[1] & (SPEC_CTRL_IBRS|SPEC_CTRL_STIBP));
				}
				cpu_mmw_pause_int(&gd->gd_reqflags, reqflags,
						  cpu_mwait_cx_hint(stat), 0);
				if (pscpu->trampoline.tr_pcb_spec_ctrl[0] &
				    SPEC_CTRL_DUMMY_ENABLE) {
					wrmsr(MSR_SPEC_CTRL, pscpu->trampoline.tr_pcb_spec_ctrl[0] & (SPEC_CTRL_IBRS|SPEC_CTRL_STIBP));
				}
				stat->halt++;
				ATOMIC_CPUMASK_NANDBIT(smp_idleinvl_mask, gd->gd_cpuid);
				if (ATOMIC_CPUMASK_TESTANDCLR(smp_idleinvl_reqs,
							      gd->gd_cpuid)) {
					cpu_invltlb();
					cpu_mfence();
				}
				crit_exit_gd(gd);
				break;
			}
			/* FALLTHROUGH */
		case 4:
			/*
			 * Use HLT
			 */
			__asm __volatile("cli");
			splz();
			crit_enter_gd(gd);
			if ((gd->gd_reqflags & RQF_IDLECHECK_WK_MASK) == 0) {
				ATOMIC_CPUMASK_ORBIT(smp_idleinvl_mask,
						     gd->gd_cpuid);
				if (pscpu->trampoline.tr_pcb_spec_ctrl[1] &
				    SPEC_CTRL_DUMMY_ENABLE) {
					wrmsr(MSR_SPEC_CTRL, pscpu->trampoline.tr_pcb_spec_ctrl[1] & (SPEC_CTRL_IBRS|SPEC_CTRL_STIBP));
				}
				cpu_idle_default_hook();
				if (pscpu->trampoline.tr_pcb_spec_ctrl[0] &
				    SPEC_CTRL_DUMMY_ENABLE) {
					wrmsr(MSR_SPEC_CTRL, pscpu->trampoline.tr_pcb_spec_ctrl[0] & (SPEC_CTRL_IBRS|SPEC_CTRL_STIBP));
				}
				ATOMIC_CPUMASK_NANDBIT(smp_idleinvl_mask,
						       gd->gd_cpuid);
				if (ATOMIC_CPUMASK_TESTANDCLR(smp_idleinvl_reqs,
							      gd->gd_cpuid)) {
					cpu_invltlb();
					cpu_mfence();
				}
			}
			__asm __volatile("sti");
			stat->halt++;
			crit_exit_gd(gd);
			break;
		case 3:
			/*
			 * Use ACPI halt
			 */
			;
do_acpi:
			__asm __volatile("cli");
			splz();
			crit_enter_gd(gd);
			if ((gd->gd_reqflags & RQF_IDLECHECK_WK_MASK) == 0) {
				ATOMIC_CPUMASK_ORBIT(smp_idleinvl_mask,
						     gd->gd_cpuid);
				if (pscpu->trampoline.tr_pcb_spec_ctrl[1] &
				    SPEC_CTRL_DUMMY_ENABLE) {
					wrmsr(MSR_SPEC_CTRL, pscpu->trampoline.tr_pcb_spec_ctrl[1] & (SPEC_CTRL_IBRS|SPEC_CTRL_STIBP));
				}
				cpu_idle_hook();
				if (pscpu->trampoline.tr_pcb_spec_ctrl[0] &
				    SPEC_CTRL_DUMMY_ENABLE) {
					wrmsr(MSR_SPEC_CTRL, pscpu->trampoline.tr_pcb_spec_ctrl[0] & (SPEC_CTRL_IBRS|SPEC_CTRL_STIBP));
				}
				ATOMIC_CPUMASK_NANDBIT(smp_idleinvl_mask,
						       gd->gd_cpuid);
				if (ATOMIC_CPUMASK_TESTANDCLR(smp_idleinvl_reqs,
							      gd->gd_cpuid)) {
					cpu_invltlb();
					cpu_mfence();
				}
			}
			__asm __volatile("sti");
			stat->halt++;
			crit_exit_gd(gd);
			break;
		}
	}
}

/*
 * Called from deep ACPI via cpu_idle_hook() (see above) to actually halt
 * the cpu in C1.  ACPI might use other halt methods for deeper states
 * and not reach here.
 *
 * For now we always use HLT as we are not sure what ACPI may have actually
 * done.  MONITOR/MWAIT might not be appropriate.
 *
 * NOTE: MONITOR/MWAIT does not appear to throttle AMD cpus, while HLT
 *	 does.  On Intel, MONITOR/MWAIT does appear to throttle the cpu.
 */
void
cpu_idle_halt(void)
{
	globaldata_t gd;

	gd = mycpu;
#if 0
	/* DISABLED FOR NOW */
	struct cpu_idle_stat *stat;
	int reqflags;


	if ((cpu_idle_hlt == 1 || cpu_idle_hlt == 2) &&
	    (cpu_mi_feature & CPU_MI_MONITOR) &&
	    cpu_vendor_id != CPU_VENDOR_AMD) {
		/*
		 * Use MONITOR/MWAIT
		 *
		 * (NOTE: On ryzen, MWAIT does not throttle clocks, so we
		 *	  have to use HLT)
		 */
		stat = &cpu_idle_stats[gd->gd_cpuid];
		reqflags = gd->gd_reqflags;
		if ((reqflags & RQF_IDLECHECK_WK_MASK) == 0) {
			__asm __volatile("sti");
			cpu_mmw_pause_int(&gd->gd_reqflags, reqflags,
					  cpu_mwait_cx_hint(stat), 0);
		} else {
			__asm __volatile("sti; pause");
		}
	} else
#endif
	{
		/*
		 * Use HLT
		 */
		if ((gd->gd_reqflags & RQF_IDLECHECK_WK_MASK) == 0)
			__asm __volatile("sti; hlt");
		else
			__asm __volatile("sti; pause");
	}
}


/*
 * Called in a loop indirectly via Xcpustop
 */
void
cpu_smp_stopped(void)
{
	globaldata_t gd = mycpu;
	volatile __uint64_t *ptr;
	__uint64_t ovalue;

	ptr = CPUMASK_ADDR(started_cpus, gd->gd_cpuid);
	ovalue = *ptr;
	if ((ovalue & CPUMASK_SIMPLE(gd->gd_cpuid & 63)) == 0) {
		if (cpu_mi_feature & CPU_MI_MONITOR) {
			if (cpu_mwait_hints) {
				cpu_mmw_pause_long(__DEVOLATILE(void *, ptr),
					   ovalue,
					   cpu_mwait_hints[
						cpu_mwait_hints_cnt - 1], 0);
			} else {
				cpu_mmw_pause_long(__DEVOLATILE(void *, ptr),
					   ovalue, 0, 0);
			}
		} else {
			cpu_halt();	/* depend on lapic timer */
		}
	}
}

/*
 * This routine is called if a spinlock has been held through the
 * exponential backoff period and is seriously contested.  On a real cpu
 * we let it spin.
 */
void
cpu_spinlock_contested(void)
{
	cpu_pause();
}

/*
 * Clear registers on exec
 */
void
exec_setregs(u_long entry, u_long stack, u_long ps_strings)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	struct pcb *pcb = td->td_pcb;
	struct trapframe *regs = lp->lwp_md.md_regs;

	user_ldt_free(pcb);

	clear_quickret();
	bzero((char *)regs, sizeof(struct trapframe));
	regs->tf_rip = entry;
	regs->tf_rsp = ((stack - 8) & ~0xFul) + 8; /* align the stack */
	regs->tf_rdi = stack;		/* argv */
	regs->tf_rflags = PSL_USER | (regs->tf_rflags & PSL_T);
	regs->tf_ss = _udatasel;
	regs->tf_cs = _ucodesel;
	regs->tf_rbx = ps_strings;

	/*
	 * Reset the hardware debug registers if they were in use.
	 * They won't have any meaning for the newly exec'd process.
	 */
	if (pcb->pcb_flags & PCB_DBREGS) {
		pcb->pcb_dr0 = 0;
		pcb->pcb_dr1 = 0;
		pcb->pcb_dr2 = 0;
		pcb->pcb_dr3 = 0;
		pcb->pcb_dr6 = 0;
		pcb->pcb_dr7 = 0; /* JG set bit 10? */
		if (pcb == td->td_pcb) {
			/*
			 * Clear the debug registers on the running
			 * CPU, otherwise they will end up affecting
			 * the next process we switch to.
			 */
			reset_dbregs();
		}
		pcb->pcb_flags &= ~PCB_DBREGS;
	}

	/*
	 * Initialize the math emulator (if any) for the current process.
	 * Actually, just clear the bit that says that the emulator has
	 * been initialized.  Initialization is delayed until the process
	 * traps to the emulator (if it is done at all) mainly because
	 * emulators don't provide an entry point for initialization.
	 */
	pcb->pcb_flags &= ~FP_SOFTFP;

	/*
	 * NOTE: do not set CR0_TS here.  npxinit() must do it after clearing
	 *	 gd_npxthread.  Otherwise a preemptive interrupt thread
	 *	 may panic in npxdna().
	 */
	crit_enter();
	load_cr0(rcr0() | CR0_MP);

	/*
	 * NOTE: The MSR values must be correct so we can return to
	 *	 userland.  gd_user_fs/gs must be correct so the switch
	 *	 code knows what the current MSR values are.
	 */
	pcb->pcb_fsbase = 0;	/* Values loaded from PCB on switch */
	pcb->pcb_gsbase = 0;
	mdcpu->gd_user_fs = 0;	/* Cache of current MSR values */
	mdcpu->gd_user_gs = 0;
	wrmsr(MSR_FSBASE, 0);	/* Set MSR values for return to userland */
	wrmsr(MSR_KGSBASE, 0);

	/* Initialize the npx (if any) for the current process. */
	npxinit();
	crit_exit();

	pcb->pcb_ds = _udatasel;
	pcb->pcb_es = _udatasel;
	pcb->pcb_fs = _udatasel;
	pcb->pcb_gs = _udatasel;
}

void
cpu_setregs(void)
{
	register_t cr0;

	cr0 = rcr0();
	cr0 |= CR0_NE;			/* Done by npxinit() */
	cr0 |= CR0_MP | CR0_TS;		/* Done at every execve() too. */
	cr0 |= CR0_WP | CR0_AM;
	load_cr0(cr0);
	load_gs(_udatasel);
}

static int
sysctl_machdep_adjkerntz(SYSCTL_HANDLER_ARGS)
{
	int error;
	error = sysctl_handle_int(oidp, oidp->oid_arg1, oidp->oid_arg2,
		req);
	if (!error && req->newptr)
		resettodr();
	return (error);
}

SYSCTL_PROC(_machdep, CPU_ADJKERNTZ, adjkerntz, CTLTYPE_INT|CTLFLAG_RW,
	&adjkerntz, 0, sysctl_machdep_adjkerntz, "I", "");

SYSCTL_INT(_machdep, CPU_DISRTCSET, disable_rtc_set,
	CTLFLAG_RW, &disable_rtc_set, 0, "");

#if 0 /* JG */
SYSCTL_STRUCT(_machdep, CPU_BOOTINFO, bootinfo,
	CTLFLAG_RD, &bootinfo, bootinfo, "");
#endif

SYSCTL_INT(_machdep, CPU_WALLCLOCK, wall_cmos_clock,
	CTLFLAG_RW, &wall_cmos_clock, 0, "");

static int
efi_map_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	struct efi_map_header *efihdr;
	caddr_t kmdp;
	uint32_t efisize;

	kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type("elf64 kernel");
	efihdr = (struct efi_map_header *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_EFI_MAP);
	if (efihdr == NULL)
		return (0);
	efisize = *((uint32_t *)efihdr - 1);
	return (SYSCTL_OUT(req, efihdr, efisize));
}
SYSCTL_PROC(_machdep, OID_AUTO, efi_map, CTLTYPE_OPAQUE|CTLFLAG_RD, NULL, 0,
    efi_map_sysctl_handler, "S,efi_map_header", "Raw EFI Memory Map");

/*
 * Initialize x86 and configure to run kernel
 */

/*
 * Initialize segments & interrupt table
 */

int _default_ldt;
struct user_segment_descriptor gdt[NGDT * MAXCPU];	/* global descriptor table */
struct gate_descriptor idt_arr[MAXCPU][NIDT];
#if 0 /* JG */
union descriptor ldt[NLDT];		/* local descriptor table */
#endif

/* table descriptors - used to load tables by cpu */
struct region_descriptor r_gdt;
struct region_descriptor r_idt_arr[MAXCPU];

/* JG proc0paddr is a virtual address */
void *proc0paddr;
/* JG alignment? */
char proc0paddr_buff[LWKT_THREAD_STACK];


/* software prototypes -- in more palatable form */
struct soft_segment_descriptor gdt_segs[] = {
/* GNULL_SEL	0 Null Descriptor */
{	0x0,			/* segment base address  */
	0x0,			/* length */
	0,			/* segment type */
	0,			/* segment descriptor priority level */
	0,			/* segment descriptor present */
	0,			/* long */
	0,			/* default 32 vs 16 bit size */
	0			/* limit granularity (byte/page units)*/ },
/* GCODE_SEL	1 Code Descriptor for kernel */
{	0x0,			/* segment base address  */
	0xfffff,		/* length - all address space */
	SDT_MEMERA,		/* segment type */
	SEL_KPL,		/* segment descriptor priority level */
	1,			/* segment descriptor present */
	1,			/* long */
	0,			/* default 32 vs 16 bit size */
	1			/* limit granularity (byte/page units)*/ },
/* GDATA_SEL	2 Data Descriptor for kernel */
{	0x0,			/* segment base address  */
	0xfffff,		/* length - all address space */
	SDT_MEMRWA,		/* segment type */
	SEL_KPL,		/* segment descriptor priority level */
	1,			/* segment descriptor present */
	1,			/* long */
	0,			/* default 32 vs 16 bit size */
	1			/* limit granularity (byte/page units)*/ },
/* GUCODE32_SEL	3 32 bit Code Descriptor for user */
{	0x0,			/* segment base address  */
	0xfffff,		/* length - all address space */
	SDT_MEMERA,		/* segment type */
	SEL_UPL,		/* segment descriptor priority level */
	1,			/* segment descriptor present */
	0,			/* long */
	1,			/* default 32 vs 16 bit size */
	1			/* limit granularity (byte/page units)*/ },
/* GUDATA_SEL	4 32/64 bit Data Descriptor for user */
{	0x0,			/* segment base address  */
	0xfffff,		/* length - all address space */
	SDT_MEMRWA,		/* segment type */
	SEL_UPL,		/* segment descriptor priority level */
	1,			/* segment descriptor present */
	0,			/* long */
	1,			/* default 32 vs 16 bit size */
	1			/* limit granularity (byte/page units)*/ },
/* GUCODE_SEL	5 64 bit Code Descriptor for user */
{	0x0,			/* segment base address  */
	0xfffff,		/* length - all address space */
	SDT_MEMERA,		/* segment type */
	SEL_UPL,		/* segment descriptor priority level */
	1,			/* segment descriptor present */
	1,			/* long */
	0,			/* default 32 vs 16 bit size */
	1			/* limit granularity (byte/page units)*/ },
/* GPROC0_SEL	6 Proc 0 Tss Descriptor */
{
	0x0,			/* segment base address */
	sizeof(struct x86_64tss)-1,/* length - all address space */
	SDT_SYSTSS,		/* segment type */
	SEL_KPL,		/* segment descriptor priority level */
	1,			/* segment descriptor present */
	0,			/* long */
	0,			/* unused - default 32 vs 16 bit size */
	0			/* limit granularity (byte/page units)*/ },
/* Actually, the TSS is a system descriptor which is double size */
{	0x0,			/* segment base address  */
	0x0,			/* length */
	0,			/* segment type */
	0,			/* segment descriptor priority level */
	0,			/* segment descriptor present */
	0,			/* long */
	0,			/* default 32 vs 16 bit size */
	0			/* limit granularity (byte/page units)*/ },
/* GUGS32_SEL	8 32 bit GS Descriptor for user */
{	0x0,			/* segment base address  */
	0xfffff,		/* length - all address space */
	SDT_MEMRWA,		/* segment type */
	SEL_UPL,		/* segment descriptor priority level */
	1,			/* segment descriptor present */
	0,			/* long */
	1,			/* default 32 vs 16 bit size */
	1			/* limit granularity (byte/page units)*/ },
};

void
setidt_global(int idx, inthand_t *func, int typ, int dpl, int ist)
{
	int cpu;

	for (cpu = 0; cpu < MAXCPU; ++cpu) {
		struct gate_descriptor *ip = &idt_arr[cpu][idx];

		ip->gd_looffset = (uintptr_t)func;
		ip->gd_selector = GSEL(GCODE_SEL, SEL_KPL);
		ip->gd_ist = ist;
		ip->gd_xx = 0;
		ip->gd_type = typ;
		ip->gd_dpl = dpl;
		ip->gd_p = 1;
		ip->gd_hioffset = ((uintptr_t)func)>>16 ;
	}
}

void
setidt(int idx, inthand_t *func, int typ, int dpl, int ist, int cpu)
{
	struct gate_descriptor *ip;

	KASSERT(cpu >= 0 && cpu < ncpus, ("invalid cpu %d", cpu));

	ip = &idt_arr[cpu][idx];
	ip->gd_looffset = (uintptr_t)func;
	ip->gd_selector = GSEL(GCODE_SEL, SEL_KPL);
	ip->gd_ist = ist;
	ip->gd_xx = 0;
	ip->gd_type = typ;
	ip->gd_dpl = dpl;
	ip->gd_p = 1;
	ip->gd_hioffset = ((uintptr_t)func)>>16 ;
}

#define	IDTVEC(name)	__CONCAT(X,name)

extern inthand_t
	IDTVEC(div), IDTVEC(dbg), IDTVEC(nmi), IDTVEC(bpt), IDTVEC(ofl),
	IDTVEC(bnd), IDTVEC(ill), IDTVEC(dna), IDTVEC(fpusegm),
	IDTVEC(tss), IDTVEC(missing), IDTVEC(stk), IDTVEC(prot),
	IDTVEC(page), IDTVEC(mchk), IDTVEC(fpu), IDTVEC(align),
	IDTVEC(xmm), IDTVEC(dblfault),
	IDTVEC(fast_syscall), IDTVEC(fast_syscall32);

extern inthand_t
	IDTVEC(rsvd00), IDTVEC(rsvd01), IDTVEC(rsvd02), IDTVEC(rsvd03),
	IDTVEC(rsvd04), IDTVEC(rsvd05), IDTVEC(rsvd06), IDTVEC(rsvd07),
	IDTVEC(rsvd08), IDTVEC(rsvd09), IDTVEC(rsvd0a), IDTVEC(rsvd0b),
	IDTVEC(rsvd0c), IDTVEC(rsvd0d), IDTVEC(rsvd0e), IDTVEC(rsvd0f),
	IDTVEC(rsvd10), IDTVEC(rsvd11), IDTVEC(rsvd12), IDTVEC(rsvd13),
	IDTVEC(rsvd14), IDTVEC(rsvd15), IDTVEC(rsvd16), IDTVEC(rsvd17),
	IDTVEC(rsvd18), IDTVEC(rsvd19), IDTVEC(rsvd1a), IDTVEC(rsvd1b),
	IDTVEC(rsvd1c), IDTVEC(rsvd1d), IDTVEC(rsvd1e), IDTVEC(rsvd1f),
	IDTVEC(rsvd20), IDTVEC(rsvd21), IDTVEC(rsvd22), IDTVEC(rsvd23),
	IDTVEC(rsvd24), IDTVEC(rsvd25), IDTVEC(rsvd26), IDTVEC(rsvd27),
	IDTVEC(rsvd28), IDTVEC(rsvd29), IDTVEC(rsvd2a), IDTVEC(rsvd2b),
	IDTVEC(rsvd2c), IDTVEC(rsvd2d), IDTVEC(rsvd2e), IDTVEC(rsvd2f),
	IDTVEC(rsvd30), IDTVEC(rsvd31), IDTVEC(rsvd32), IDTVEC(rsvd33),
	IDTVEC(rsvd34), IDTVEC(rsvd35), IDTVEC(rsvd36), IDTVEC(rsvd37),
	IDTVEC(rsvd38), IDTVEC(rsvd39), IDTVEC(rsvd3a), IDTVEC(rsvd3b),
	IDTVEC(rsvd3c), IDTVEC(rsvd3d), IDTVEC(rsvd3e), IDTVEC(rsvd3f),
	IDTVEC(rsvd40), IDTVEC(rsvd41), IDTVEC(rsvd42), IDTVEC(rsvd43),
	IDTVEC(rsvd44), IDTVEC(rsvd45), IDTVEC(rsvd46), IDTVEC(rsvd47),
	IDTVEC(rsvd48), IDTVEC(rsvd49), IDTVEC(rsvd4a), IDTVEC(rsvd4b),
	IDTVEC(rsvd4c), IDTVEC(rsvd4d), IDTVEC(rsvd4e), IDTVEC(rsvd4f),
	IDTVEC(rsvd50), IDTVEC(rsvd51), IDTVEC(rsvd52), IDTVEC(rsvd53),
	IDTVEC(rsvd54), IDTVEC(rsvd55), IDTVEC(rsvd56), IDTVEC(rsvd57),
	IDTVEC(rsvd58), IDTVEC(rsvd59), IDTVEC(rsvd5a), IDTVEC(rsvd5b),
	IDTVEC(rsvd5c), IDTVEC(rsvd5d), IDTVEC(rsvd5e), IDTVEC(rsvd5f),
	IDTVEC(rsvd60), IDTVEC(rsvd61), IDTVEC(rsvd62), IDTVEC(rsvd63),
	IDTVEC(rsvd64), IDTVEC(rsvd65), IDTVEC(rsvd66), IDTVEC(rsvd67),
	IDTVEC(rsvd68), IDTVEC(rsvd69), IDTVEC(rsvd6a), IDTVEC(rsvd6b),
	IDTVEC(rsvd6c), IDTVEC(rsvd6d), IDTVEC(rsvd6e), IDTVEC(rsvd6f),
	IDTVEC(rsvd70), IDTVEC(rsvd71), IDTVEC(rsvd72), IDTVEC(rsvd73),
	IDTVEC(rsvd74), IDTVEC(rsvd75), IDTVEC(rsvd76), IDTVEC(rsvd77),
	IDTVEC(rsvd78), IDTVEC(rsvd79), IDTVEC(rsvd7a), IDTVEC(rsvd7b),
	IDTVEC(rsvd7c), IDTVEC(rsvd7d), IDTVEC(rsvd7e), IDTVEC(rsvd7f),
	IDTVEC(rsvd80), IDTVEC(rsvd81), IDTVEC(rsvd82), IDTVEC(rsvd83),
	IDTVEC(rsvd84), IDTVEC(rsvd85), IDTVEC(rsvd86), IDTVEC(rsvd87),
	IDTVEC(rsvd88), IDTVEC(rsvd89), IDTVEC(rsvd8a), IDTVEC(rsvd8b),
	IDTVEC(rsvd8c), IDTVEC(rsvd8d), IDTVEC(rsvd8e), IDTVEC(rsvd8f),
	IDTVEC(rsvd90), IDTVEC(rsvd91), IDTVEC(rsvd92), IDTVEC(rsvd93),
	IDTVEC(rsvd94), IDTVEC(rsvd95), IDTVEC(rsvd96), IDTVEC(rsvd97),
	IDTVEC(rsvd98), IDTVEC(rsvd99), IDTVEC(rsvd9a), IDTVEC(rsvd9b),
	IDTVEC(rsvd9c), IDTVEC(rsvd9d), IDTVEC(rsvd9e), IDTVEC(rsvd9f),
	IDTVEC(rsvda0), IDTVEC(rsvda1), IDTVEC(rsvda2), IDTVEC(rsvda3),
	IDTVEC(rsvda4), IDTVEC(rsvda5), IDTVEC(rsvda6), IDTVEC(rsvda7),
	IDTVEC(rsvda8), IDTVEC(rsvda9), IDTVEC(rsvdaa), IDTVEC(rsvdab),
	IDTVEC(rsvdac), IDTVEC(rsvdad), IDTVEC(rsvdae), IDTVEC(rsvdaf),
	IDTVEC(rsvdb0), IDTVEC(rsvdb1), IDTVEC(rsvdb2), IDTVEC(rsvdb3),
	IDTVEC(rsvdb4), IDTVEC(rsvdb5), IDTVEC(rsvdb6), IDTVEC(rsvdb7),
	IDTVEC(rsvdb8), IDTVEC(rsvdb9), IDTVEC(rsvdba), IDTVEC(rsvdbb),
	IDTVEC(rsvdbc), IDTVEC(rsvdbd), IDTVEC(rsvdbe), IDTVEC(rsvdbf),
	IDTVEC(rsvdc0), IDTVEC(rsvdc1), IDTVEC(rsvdc2), IDTVEC(rsvdc3),
	IDTVEC(rsvdc4), IDTVEC(rsvdc5), IDTVEC(rsvdc6), IDTVEC(rsvdc7),
	IDTVEC(rsvdc8), IDTVEC(rsvdc9), IDTVEC(rsvdca), IDTVEC(rsvdcb),
	IDTVEC(rsvdcc), IDTVEC(rsvdcd), IDTVEC(rsvdce), IDTVEC(rsvdcf),
	IDTVEC(rsvdd0), IDTVEC(rsvdd1), IDTVEC(rsvdd2), IDTVEC(rsvdd3),
	IDTVEC(rsvdd4), IDTVEC(rsvdd5), IDTVEC(rsvdd6), IDTVEC(rsvdd7),
	IDTVEC(rsvdd8), IDTVEC(rsvdd9), IDTVEC(rsvdda), IDTVEC(rsvddb),
	IDTVEC(rsvddc), IDTVEC(rsvddd), IDTVEC(rsvdde), IDTVEC(rsvddf),
	IDTVEC(rsvde0), IDTVEC(rsvde1), IDTVEC(rsvde2), IDTVEC(rsvde3),
	IDTVEC(rsvde4), IDTVEC(rsvde5), IDTVEC(rsvde6), IDTVEC(rsvde7),
	IDTVEC(rsvde8), IDTVEC(rsvde9), IDTVEC(rsvdea), IDTVEC(rsvdeb),
	IDTVEC(rsvdec), IDTVEC(rsvded), IDTVEC(rsvdee), IDTVEC(rsvdef),
	IDTVEC(rsvdf0), IDTVEC(rsvdf1), IDTVEC(rsvdf2), IDTVEC(rsvdf3),
	IDTVEC(rsvdf4), IDTVEC(rsvdf5), IDTVEC(rsvdf6), IDTVEC(rsvdf7),
	IDTVEC(rsvdf8), IDTVEC(rsvdf9), IDTVEC(rsvdfa), IDTVEC(rsvdfb),
	IDTVEC(rsvdfc), IDTVEC(rsvdfd), IDTVEC(rsvdfe), IDTVEC(rsvdff);

inthand_t *rsvdary[NIDT] = {
	&IDTVEC(rsvd00), &IDTVEC(rsvd01), &IDTVEC(rsvd02), &IDTVEC(rsvd03),
	&IDTVEC(rsvd04), &IDTVEC(rsvd05), &IDTVEC(rsvd06), &IDTVEC(rsvd07),
	&IDTVEC(rsvd08), &IDTVEC(rsvd09), &IDTVEC(rsvd0a), &IDTVEC(rsvd0b),
	&IDTVEC(rsvd0c), &IDTVEC(rsvd0d), &IDTVEC(rsvd0e), &IDTVEC(rsvd0f),
	&IDTVEC(rsvd10), &IDTVEC(rsvd11), &IDTVEC(rsvd12), &IDTVEC(rsvd13),
	&IDTVEC(rsvd14), &IDTVEC(rsvd15), &IDTVEC(rsvd16), &IDTVEC(rsvd17),
	&IDTVEC(rsvd18), &IDTVEC(rsvd19), &IDTVEC(rsvd1a), &IDTVEC(rsvd1b),
	&IDTVEC(rsvd1c), &IDTVEC(rsvd1d), &IDTVEC(rsvd1e), &IDTVEC(rsvd1f),
	&IDTVEC(rsvd20), &IDTVEC(rsvd21), &IDTVEC(rsvd22), &IDTVEC(rsvd23),
	&IDTVEC(rsvd24), &IDTVEC(rsvd25), &IDTVEC(rsvd26), &IDTVEC(rsvd27),
	&IDTVEC(rsvd28), &IDTVEC(rsvd29), &IDTVEC(rsvd2a), &IDTVEC(rsvd2b),
	&IDTVEC(rsvd2c), &IDTVEC(rsvd2d), &IDTVEC(rsvd2e), &IDTVEC(rsvd2f),
	&IDTVEC(rsvd30), &IDTVEC(rsvd31), &IDTVEC(rsvd32), &IDTVEC(rsvd33),
	&IDTVEC(rsvd34), &IDTVEC(rsvd35), &IDTVEC(rsvd36), &IDTVEC(rsvd37),
	&IDTVEC(rsvd38), &IDTVEC(rsvd39), &IDTVEC(rsvd3a), &IDTVEC(rsvd3b),
	&IDTVEC(rsvd3c), &IDTVEC(rsvd3d), &IDTVEC(rsvd3e), &IDTVEC(rsvd3f),
	&IDTVEC(rsvd40), &IDTVEC(rsvd41), &IDTVEC(rsvd42), &IDTVEC(rsvd43),
	&IDTVEC(rsvd44), &IDTVEC(rsvd45), &IDTVEC(rsvd46), &IDTVEC(rsvd47),
	&IDTVEC(rsvd48), &IDTVEC(rsvd49), &IDTVEC(rsvd4a), &IDTVEC(rsvd4b),
	&IDTVEC(rsvd4c), &IDTVEC(rsvd4d), &IDTVEC(rsvd4e), &IDTVEC(rsvd4f),
	&IDTVEC(rsvd50), &IDTVEC(rsvd51), &IDTVEC(rsvd52), &IDTVEC(rsvd53),
	&IDTVEC(rsvd54), &IDTVEC(rsvd55), &IDTVEC(rsvd56), &IDTVEC(rsvd57),
	&IDTVEC(rsvd58), &IDTVEC(rsvd59), &IDTVEC(rsvd5a), &IDTVEC(rsvd5b),
	&IDTVEC(rsvd5c), &IDTVEC(rsvd5d), &IDTVEC(rsvd5e), &IDTVEC(rsvd5f),
	&IDTVEC(rsvd60), &IDTVEC(rsvd61), &IDTVEC(rsvd62), &IDTVEC(rsvd63),
	&IDTVEC(rsvd64), &IDTVEC(rsvd65), &IDTVEC(rsvd66), &IDTVEC(rsvd67),
	&IDTVEC(rsvd68), &IDTVEC(rsvd69), &IDTVEC(rsvd6a), &IDTVEC(rsvd6b),
	&IDTVEC(rsvd6c), &IDTVEC(rsvd6d), &IDTVEC(rsvd6e), &IDTVEC(rsvd6f),
	&IDTVEC(rsvd70), &IDTVEC(rsvd71), &IDTVEC(rsvd72), &IDTVEC(rsvd73),
	&IDTVEC(rsvd74), &IDTVEC(rsvd75), &IDTVEC(rsvd76), &IDTVEC(rsvd77),
	&IDTVEC(rsvd78), &IDTVEC(rsvd79), &IDTVEC(rsvd7a), &IDTVEC(rsvd7b),
	&IDTVEC(rsvd7c), &IDTVEC(rsvd7d), &IDTVEC(rsvd7e), &IDTVEC(rsvd7f),
	&IDTVEC(rsvd80), &IDTVEC(rsvd81), &IDTVEC(rsvd82), &IDTVEC(rsvd83),
	&IDTVEC(rsvd84), &IDTVEC(rsvd85), &IDTVEC(rsvd86), &IDTVEC(rsvd87),
	&IDTVEC(rsvd88), &IDTVEC(rsvd89), &IDTVEC(rsvd8a), &IDTVEC(rsvd8b),
	&IDTVEC(rsvd8c), &IDTVEC(rsvd8d), &IDTVEC(rsvd8e), &IDTVEC(rsvd8f),
	&IDTVEC(rsvd90), &IDTVEC(rsvd91), &IDTVEC(rsvd92), &IDTVEC(rsvd93),
	&IDTVEC(rsvd94), &IDTVEC(rsvd95), &IDTVEC(rsvd96), &IDTVEC(rsvd97),
	&IDTVEC(rsvd98), &IDTVEC(rsvd99), &IDTVEC(rsvd9a), &IDTVEC(rsvd9b),
	&IDTVEC(rsvd9c), &IDTVEC(rsvd9d), &IDTVEC(rsvd9e), &IDTVEC(rsvd9f),
	&IDTVEC(rsvda0), &IDTVEC(rsvda1), &IDTVEC(rsvda2), &IDTVEC(rsvda3),
	&IDTVEC(rsvda4), &IDTVEC(rsvda5), &IDTVEC(rsvda6), &IDTVEC(rsvda7),
	&IDTVEC(rsvda8), &IDTVEC(rsvda9), &IDTVEC(rsvdaa), &IDTVEC(rsvdab),
	&IDTVEC(rsvdac), &IDTVEC(rsvdad), &IDTVEC(rsvdae), &IDTVEC(rsvdaf),
	&IDTVEC(rsvdb0), &IDTVEC(rsvdb1), &IDTVEC(rsvdb2), &IDTVEC(rsvdb3),
	&IDTVEC(rsvdb4), &IDTVEC(rsvdb5), &IDTVEC(rsvdb6), &IDTVEC(rsvdb7),
	&IDTVEC(rsvdb8), &IDTVEC(rsvdb9), &IDTVEC(rsvdba), &IDTVEC(rsvdbb),
	&IDTVEC(rsvdbc), &IDTVEC(rsvdbd), &IDTVEC(rsvdbe), &IDTVEC(rsvdbf),
	&IDTVEC(rsvdc0), &IDTVEC(rsvdc1), &IDTVEC(rsvdc2), &IDTVEC(rsvdc3),
	&IDTVEC(rsvdc4), &IDTVEC(rsvdc5), &IDTVEC(rsvdc6), &IDTVEC(rsvdc7),
	&IDTVEC(rsvdc8), &IDTVEC(rsvdc9), &IDTVEC(rsvdca), &IDTVEC(rsvdcb),
	&IDTVEC(rsvdcc), &IDTVEC(rsvdcd), &IDTVEC(rsvdce), &IDTVEC(rsvdcf),
	&IDTVEC(rsvdd0), &IDTVEC(rsvdd1), &IDTVEC(rsvdd2), &IDTVEC(rsvdd3),
	&IDTVEC(rsvdd4), &IDTVEC(rsvdd5), &IDTVEC(rsvdd6), &IDTVEC(rsvdd7),
	&IDTVEC(rsvdd8), &IDTVEC(rsvdd9), &IDTVEC(rsvdda), &IDTVEC(rsvddb),
	&IDTVEC(rsvddc), &IDTVEC(rsvddd), &IDTVEC(rsvdde), &IDTVEC(rsvddf),
	&IDTVEC(rsvde0), &IDTVEC(rsvde1), &IDTVEC(rsvde2), &IDTVEC(rsvde3),
	&IDTVEC(rsvde4), &IDTVEC(rsvde5), &IDTVEC(rsvde6), &IDTVEC(rsvde7),
	&IDTVEC(rsvde8), &IDTVEC(rsvde9), &IDTVEC(rsvdea), &IDTVEC(rsvdeb),
	&IDTVEC(rsvdec), &IDTVEC(rsvded), &IDTVEC(rsvdee), &IDTVEC(rsvdef),
	&IDTVEC(rsvdf0), &IDTVEC(rsvdf1), &IDTVEC(rsvdf2), &IDTVEC(rsvdf3),
	&IDTVEC(rsvdf4), &IDTVEC(rsvdf5), &IDTVEC(rsvdf6), &IDTVEC(rsvdf7),
	&IDTVEC(rsvdf8), &IDTVEC(rsvdf9), &IDTVEC(rsvdfa), &IDTVEC(rsvdfb),
	&IDTVEC(rsvdfc), &IDTVEC(rsvdfd), &IDTVEC(rsvdfe), &IDTVEC(rsvdff)
};

void
sdtossd(struct user_segment_descriptor *sd, struct soft_segment_descriptor *ssd)
{
	ssd->ssd_base  = (sd->sd_hibase << 24) | sd->sd_lobase;
	ssd->ssd_limit = (sd->sd_hilimit << 16) | sd->sd_lolimit;
	ssd->ssd_type  = sd->sd_type;
	ssd->ssd_dpl   = sd->sd_dpl;
	ssd->ssd_p     = sd->sd_p;
	ssd->ssd_def32 = sd->sd_def32;
	ssd->ssd_gran  = sd->sd_gran;
}

void
ssdtosd(struct soft_segment_descriptor *ssd, struct user_segment_descriptor *sd)
{

	sd->sd_lobase = (ssd->ssd_base) & 0xffffff;
	sd->sd_hibase = (ssd->ssd_base >> 24) & 0xff;
	sd->sd_lolimit = (ssd->ssd_limit) & 0xffff;
	sd->sd_hilimit = (ssd->ssd_limit >> 16) & 0xf;
	sd->sd_type  = ssd->ssd_type;
	sd->sd_dpl   = ssd->ssd_dpl;
	sd->sd_p     = ssd->ssd_p;
	sd->sd_long  = ssd->ssd_long;
	sd->sd_def32 = ssd->ssd_def32;
	sd->sd_gran  = ssd->ssd_gran;
}

void
ssdtosyssd(struct soft_segment_descriptor *ssd,
    struct system_segment_descriptor *sd)
{

	sd->sd_lobase = (ssd->ssd_base) & 0xffffff;
	sd->sd_hibase = (ssd->ssd_base >> 24) & 0xfffffffffful;
	sd->sd_lolimit = (ssd->ssd_limit) & 0xffff;
	sd->sd_hilimit = (ssd->ssd_limit >> 16) & 0xf;
	sd->sd_type  = ssd->ssd_type;
	sd->sd_dpl   = ssd->ssd_dpl;
	sd->sd_p     = ssd->ssd_p;
	sd->sd_gran  = ssd->ssd_gran;
}

/*
 * Populate the (physmap) array with base/bound pairs describing the
 * available physical memory in the system, then test this memory and
 * build the phys_avail array describing the actually-available memory.
 *
 * If we cannot accurately determine the physical memory map, then use
 * value from the 0xE801 call, and failing that, the RTC.
 *
 * Total memory size may be set by the kernel environment variable
 * hw.physmem or the compile-time define MAXMEM.
 *
 * Memory is aligned to PHYSMAP_ALIGN which must be a multiple
 * of PAGE_SIZE.  This also greatly reduces the memory test time
 * which would otherwise be excessive on machines with > 8G of ram.
 *
 * XXX first should be vm_paddr_t.
 */

#define PHYSMAP_ALIGN		(vm_paddr_t)(128 * 1024)
#define PHYSMAP_ALIGN_MASK	(vm_paddr_t)(PHYSMAP_ALIGN - 1)
#define PHYSMAP_SIZE		VM_PHYSSEG_MAX

vm_paddr_t physmap[PHYSMAP_SIZE];
struct bios_smap *smapbase, *smap, *smapend;
struct efi_map_header *efihdrbase;
u_int32_t smapsize;

#define PHYSMAP_HANDWAVE	(vm_paddr_t)(2 * 1024 * 1024)
#define PHYSMAP_HANDWAVE_MASK	(PHYSMAP_HANDWAVE - 1)

static void
add_smap_entries(int *physmap_idx)
{
	int i;

	smapsize = *((u_int32_t *)smapbase - 1);
	smapend = (struct bios_smap *)((uintptr_t)smapbase + smapsize);

	for (smap = smapbase; smap < smapend; smap++) {
		if (boothowto & RB_VERBOSE)
			kprintf("SMAP type=%02x base=%016lx len=%016lx\n",
			    smap->type, smap->base, smap->length);

		if (smap->type != SMAP_TYPE_MEMORY)
			continue;

		if (smap->length == 0)
			continue;

		for (i = 0; i <= *physmap_idx; i += 2) {
			if (smap->base < physmap[i + 1]) {
				if (boothowto & RB_VERBOSE) {
					kprintf("Overlapping or non-monotonic "
						"memory region, ignoring "
						"second region\n");
				}
				break;
			}
		}
		if (i <= *physmap_idx)
			continue;

		Realmem += smap->length;

		/*
		 * NOTE: This little bit of code initially expands
		 *	 physmap[1] as well as later entries.
		 */
		if (smap->base == physmap[*physmap_idx + 1]) {
			physmap[*physmap_idx + 1] += smap->length;
			continue;
		}

		*physmap_idx += 2;
		if (*physmap_idx == PHYSMAP_SIZE) {
			kprintf("Too many segments in the physical "
				"address map, giving up\n");
			break;
		}
		physmap[*physmap_idx] = smap->base;
		physmap[*physmap_idx + 1] = smap->base + smap->length;
	}
}

static void
add_efi_map_entries(int *physmap_idx)
{
	struct efi_md *map, *p;
	const char *type;
	size_t efisz;
	int i, ndesc;

	static const char *types[] = {
		"Reserved",
		"LoaderCode",
		"LoaderData",
		"BootServicesCode",
		"BootServicesData",
		"RuntimeServicesCode",
		"RuntimeServicesData",
		"ConventionalMemory",
		"UnusableMemory",
		"ACPIReclaimMemory",
		"ACPIMemoryNVS",
		"MemoryMappedIO",
		"MemoryMappedIOPortSpace",
		"PalCode"
	 };

	/*
	 * Memory map data provided by UEFI via the GetMemoryMap
	 * Boot Services API.
	 */
	efisz = (sizeof(struct efi_map_header) + 0xf) & ~0xf;
	map = (struct efi_md *)((uint8_t *)efihdrbase + efisz);

	if (efihdrbase->descriptor_size == 0)
		return;
	ndesc = efihdrbase->memory_size / efihdrbase->descriptor_size;

	if (boothowto & RB_VERBOSE)
		kprintf("%23s %12s %12s %8s %4s\n",
		    "Type", "Physical", "Virtual", "#Pages", "Attr");

	for (i = 0, p = map; i < ndesc; i++,
	    p = efi_next_descriptor(p, efihdrbase->descriptor_size)) {
		if (boothowto & RB_VERBOSE) {
			if (p->md_type <= EFI_MD_TYPE_PALCODE)
				type = types[p->md_type];
			else
				type = "<INVALID>";
			kprintf("%23s %012lx %12p %08lx ", type, p->md_phys,
			    p->md_virt, p->md_pages);
			if (p->md_attr & EFI_MD_ATTR_UC)
				kprintf("UC ");
			if (p->md_attr & EFI_MD_ATTR_WC)
				kprintf("WC ");
			if (p->md_attr & EFI_MD_ATTR_WT)
				kprintf("WT ");
			if (p->md_attr & EFI_MD_ATTR_WB)
				kprintf("WB ");
			if (p->md_attr & EFI_MD_ATTR_UCE)
				kprintf("UCE ");
			if (p->md_attr & EFI_MD_ATTR_WP)
				kprintf("WP ");
			if (p->md_attr & EFI_MD_ATTR_RP)
				kprintf("RP ");
			if (p->md_attr & EFI_MD_ATTR_XP)
				kprintf("XP ");
			if (p->md_attr & EFI_MD_ATTR_RT)
				kprintf("RUNTIME");
			kprintf("\n");
		}

		switch (p->md_type) {
		case EFI_MD_TYPE_CODE:
		case EFI_MD_TYPE_DATA:
		case EFI_MD_TYPE_BS_CODE:
		case EFI_MD_TYPE_BS_DATA:
		case EFI_MD_TYPE_FREE:
			/*
			 * We're allowed to use any entry with these types.
			 */
			break;
		default:
			continue;
		}

		Realmem += p->md_pages * PAGE_SIZE;

		/*
		 * NOTE: This little bit of code initially expands
		 *	 physmap[1] as well as later entries.
		 */
		if (p->md_phys == physmap[*physmap_idx + 1]) {
			physmap[*physmap_idx + 1] += p->md_pages * PAGE_SIZE;
			continue;
		}

		*physmap_idx += 2;
		if (*physmap_idx == PHYSMAP_SIZE) {
			kprintf("Too many segments in the physical "
				"address map, giving up\n");
			break;
		}
		physmap[*physmap_idx] = p->md_phys;
		physmap[*physmap_idx + 1] = p->md_phys + p->md_pages * PAGE_SIZE;
	 }
}

struct fb_info efi_fb_info;
static int have_efi_framebuffer = 0;

static void
efi_fb_init_vaddr(int direct_map)
{
	uint64_t sz;
	vm_offset_t addr, v;

	v = efi_fb_info.vaddr;
	sz = efi_fb_info.stride * efi_fb_info.height;

	if (direct_map) {
		addr = PHYS_TO_DMAP(efi_fb_info.paddr);
		if (addr >= DMAP_MIN_ADDRESS && addr + sz <= DMapMaxAddress)
			efi_fb_info.vaddr = addr;
	} else {
		efi_fb_info.vaddr =
			(vm_offset_t)pmap_mapdev_attr(efi_fb_info.paddr,
						      sz,
						      PAT_WRITE_COMBINING);
	}
}

static u_int
efifb_color_depth(struct efi_fb *efifb)
{
	uint32_t mask;
	u_int depth;

	mask = efifb->fb_mask_red | efifb->fb_mask_green |
	    efifb->fb_mask_blue | efifb->fb_mask_reserved;
	if (mask == 0)
		return (0);
	for (depth = 1; mask != 1; depth++)
		mask >>= 1;
	return (depth);
}

int
probe_efi_fb(int early)
{
	struct efi_fb	*efifb;
	caddr_t		kmdp;
	u_int		depth;

	if (have_efi_framebuffer) {
		if (!early &&
		    (efi_fb_info.vaddr == 0 ||
		     efi_fb_info.vaddr == PHYS_TO_DMAP(efi_fb_info.paddr)))
			efi_fb_init_vaddr(0);
		return 0;
	}

	kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type("elf64 kernel");
	efifb = (struct efi_fb *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_EFI_FB);
	if (efifb == NULL)
		return 1;

	depth = efifb_color_depth(efifb);
	/*
	 * Our bootloader should already notice, when we won't be able to
	 * use the UEFI framebuffer.
	 */
	if (depth != 24 && depth != 32)
		return 1;

	have_efi_framebuffer = 1;

	efi_fb_info.is_vga_boot_display = 1;
	efi_fb_info.width = efifb->fb_width;
	efi_fb_info.height = efifb->fb_height;
	efi_fb_info.depth = depth;
	efi_fb_info.stride = efifb->fb_stride * (depth / 8);
	efi_fb_info.paddr = efifb->fb_addr;
	if (early) {
		efi_fb_info.vaddr = 0;
	} else {
		efi_fb_init_vaddr(0);
	}
	efi_fb_info.fbops.fb_set_par = NULL;
	efi_fb_info.fbops.fb_blank = NULL;
	efi_fb_info.fbops.fb_debug_enter = NULL;
	efi_fb_info.device = NULL;

	return 0;
}

static void
efifb_startup(void *arg)
{
	probe_efi_fb(0);
}

SYSINIT(efi_fb_info, SI_BOOT1_POST, SI_ORDER_FIRST, efifb_startup, NULL);

static void
getmemsize(caddr_t kmdp, u_int64_t first)
{
	int off, physmap_idx, pa_indx, da_indx;
	int i, j;
	vm_paddr_t pa;
	vm_paddr_t msgbuf_size;
	u_long physmem_tunable;
	pt_entry_t *pte;
	quad_t dcons_addr, dcons_size;

	bzero(physmap, sizeof(physmap));
	physmap_idx = 0;

	/*
	 * get memory map from INT 15:E820, kindly supplied by the loader.
	 *
	 * subr_module.c says:
	 * "Consumer may safely assume that size value precedes data."
	 * ie: an int32_t immediately precedes smap.
	 */
	efihdrbase = (struct efi_map_header *)preload_search_info(kmdp,
		     MODINFO_METADATA | MODINFOMD_EFI_MAP);
	smapbase = (struct bios_smap *)preload_search_info(kmdp,
		   MODINFO_METADATA | MODINFOMD_SMAP);
	if (smapbase == NULL && efihdrbase == NULL)
		panic("No BIOS smap or EFI map info from loader!");

	if (efihdrbase == NULL)
		add_smap_entries(&physmap_idx);
	else
		add_efi_map_entries(&physmap_idx);

	base_memory = physmap[1] / 1024;
	/* make hole for AP bootstrap code */
	physmap[1] = mp_bootaddress(base_memory);

	/* Save EBDA address, if any */
	ebda_addr = (u_long)(*(u_short *)(KERNBASE + 0x40e));
	ebda_addr <<= 4;

	/*
	 * Maxmem isn't the "maximum memory", it's one larger than the
	 * highest page of the physical address space.  It should be
	 * called something like "Maxphyspage".  We may adjust this
	 * based on ``hw.physmem'' and the results of the memory test.
	 */
	Maxmem = atop(physmap[physmap_idx + 1]);

#ifdef MAXMEM
	Maxmem = MAXMEM / 4;
#endif

	if (TUNABLE_ULONG_FETCH("hw.physmem", &physmem_tunable))
		Maxmem = atop(physmem_tunable);

	/*
	 * Don't allow MAXMEM or hw.physmem to extend the amount of memory
	 * in the system.
	 */
	if (Maxmem > atop(physmap[physmap_idx + 1]))
		Maxmem = atop(physmap[physmap_idx + 1]);

	/*
	 * Blowing out the DMAP will blow up the system.
	 */
	if (Maxmem > atop(DMAP_MAX_ADDRESS - DMAP_MIN_ADDRESS)) {
		kprintf("Limiting Maxmem due to DMAP size\n");
		Maxmem = atop(DMAP_MAX_ADDRESS - DMAP_MIN_ADDRESS);
	}

	if (atop(physmap[physmap_idx + 1]) != Maxmem &&
	    (boothowto & RB_VERBOSE)) {
		kprintf("Physical memory use set to %ldK\n", Maxmem * 4);
	}

	/*
	 * Call pmap initialization to make new kernel address space
	 *
	 * Mask off page 0.
	 */
	pmap_bootstrap(&first);
	physmap[0] = PAGE_SIZE;

	/*
	 * Align the physmap to PHYSMAP_ALIGN and cut out anything
	 * exceeding Maxmem.
	 */
	for (i = j = 0; i <= physmap_idx; i += 2) {
		if (physmap[i+1] > ptoa(Maxmem))
			physmap[i+1] = ptoa(Maxmem);
		physmap[i] = (physmap[i] + PHYSMAP_ALIGN_MASK) &
			     ~PHYSMAP_ALIGN_MASK;
		physmap[i+1] = physmap[i+1] & ~PHYSMAP_ALIGN_MASK;

		physmap[j] = physmap[i];
		physmap[j+1] = physmap[i+1];

		if (physmap[i] < physmap[i+1])
			j += 2;
	}
	physmap_idx = j - 2;

	/*
	 * Align anything else used in the validation loop.
	 *
	 * Also make sure that our 2MB kernel text+data+bss mappings
	 * do not overlap potentially allocatable space.
	 */
	first = (first + PHYSMAP_ALIGN_MASK) & ~PHYSMAP_ALIGN_MASK;

	/*
	 * Size up each available chunk of physical memory.
	 */
	pa_indx = 0;
	da_indx = 0;
	phys_avail[pa_indx].phys_beg = physmap[0];
	phys_avail[pa_indx].phys_end = physmap[0];
	dump_avail[da_indx].phys_beg = 0;
	dump_avail[da_indx].phys_end = physmap[0];
	pte = CMAP1;

	/*
	 * Get dcons buffer address
	 */
	if (kgetenv_quad("dcons.addr", &dcons_addr) == 0 ||
	    kgetenv_quad("dcons.size", &dcons_size) == 0)
		dcons_addr = 0;

	/*
	 * Validate the physical memory.  The physical memory segments
	 * have already been aligned to PHYSMAP_ALIGN which is a multiple
	 * of PAGE_SIZE.
	 *
	 * We no longer perform an exhaustive memory test.  Instead we
	 * simply test the first and last word in each physmap[]
	 * segment.
	 */
	for (i = 0; i <= physmap_idx; i += 2) {
		vm_paddr_t end;
		vm_paddr_t incr;

		end = physmap[i + 1];

		for (pa = physmap[i]; pa < end; pa += incr) {
			int page_bad, full;
			volatile uint64_t *ptr = (uint64_t *)CADDR1;
			uint64_t tmp;

			full = FALSE;

			/*
			 * Calculate incr.  Just test the first and
			 * last page in each physmap[] segment.
			 */
			if (pa == end - PAGE_SIZE)
				incr = PAGE_SIZE;
			else
				incr = end - pa - PAGE_SIZE;

			/*
			 * Make sure we don't skip blacked out areas.
			 */
			if (pa < 0x200000 && 0x200000 < end) {
				incr = 0x200000 - pa;
			}
			if (dcons_addr > 0 &&
			    pa < dcons_addr &&
			    dcons_addr < end) {
				incr = dcons_addr - pa;
			}

			/*
			 * Block out kernel memory as not available.
			 */
			if (pa >= 0x200000 && pa < first) {
				incr = first - pa;
				if (pa + incr > end)
					incr = end - pa;
				goto do_dump_avail;
			}

			/*
			 * Block out the dcons buffer if it exists.
			 */
			if (dcons_addr > 0 &&
			    pa >= trunc_page(dcons_addr) &&
			    pa < dcons_addr + dcons_size) {
				incr = dcons_addr + dcons_size - pa;
				incr = (incr + PAGE_MASK) &
				       ~(vm_paddr_t)PAGE_MASK;
				if (pa + incr > end)
					incr = end - pa;
				goto do_dump_avail;
			}

			page_bad = FALSE;

			/*
			 * Map the page non-cacheable for the memory
			 * test.
			 */
			*pte = pa |
			    kernel_pmap.pmap_bits[PG_V_IDX] |
			    kernel_pmap.pmap_bits[PG_RW_IDX] |
			    kernel_pmap.pmap_bits[PG_N_IDX];
			cpu_invlpg(__DEVOLATILE(void *, ptr));
			cpu_mfence();

			/*
			 * Save original value for restoration later.
			 */
			tmp = *ptr;

			/*
			 * Test for alternating 1's and 0's
			 */
			*ptr = 0xaaaaaaaaaaaaaaaaLLU;
			cpu_mfence();
			if (*ptr != 0xaaaaaaaaaaaaaaaaLLU)
				page_bad = TRUE;
			/*
			 * Test for alternating 0's and 1's
			 */
			*ptr = 0x5555555555555555LLU;
			cpu_mfence();
			if (*ptr != 0x5555555555555555LLU)
				page_bad = TRUE;
			/*
			 * Test for all 1's
			 */
			*ptr = 0xffffffffffffffffLLU;
			cpu_mfence();
			if (*ptr != 0xffffffffffffffffLLU)
				page_bad = TRUE;
			/*
			 * Test for all 0's
			 */
			*ptr = 0x0;
			cpu_mfence();
			if (*ptr != 0x0)
				page_bad = TRUE;

			/*
			 * Restore original value.
			 */
			*ptr = tmp;

			/*
			 * Adjust array of valid/good pages.
			 */
			if (page_bad == TRUE) {
				incr = PAGE_SIZE;
				continue;
			}

			/*
			 * Collapse page address into phys_avail[].  Do a
			 * continuation of the current phys_avail[] index
			 * when possible.
			 */
			if (phys_avail[pa_indx].phys_end == pa) {
				/*
				 * Continuation
				 */
				phys_avail[pa_indx].phys_end += incr;
			} else if (phys_avail[pa_indx].phys_beg ==
				   phys_avail[pa_indx].phys_end) {
				/*
				 * Current phys_avail is completely empty,
				 * reuse the index.
				 */
				phys_avail[pa_indx].phys_beg = pa;
				phys_avail[pa_indx].phys_end = pa + incr;
			} else {
				/*
				 * Allocate next phys_avail index.
				 */
				++pa_indx;
				if (pa_indx == PHYS_AVAIL_ARRAY_END) {
					kprintf(
		"Too many holes in the physical address space, giving up\n");
					--pa_indx;
					full = TRUE;
					goto do_dump_avail;
				}
				phys_avail[pa_indx].phys_beg = pa;
				phys_avail[pa_indx].phys_end = pa + incr;
			}
			physmem += incr / PAGE_SIZE;

			/*
			 * pa available for dumping
			 */
do_dump_avail:
			if (dump_avail[da_indx].phys_end == pa) {
				dump_avail[da_indx].phys_end += incr;
			} else {
				++da_indx;
				if (da_indx == DUMP_AVAIL_ARRAY_END) {
					--da_indx;
					goto do_next;
				}
				dump_avail[da_indx].phys_beg = pa;
				dump_avail[da_indx].phys_end = pa + incr;
			}
do_next:
			if (full)
				break;
		}
	}
	*pte = 0;
	cpu_invltlb();
	cpu_mfence();

	/*
	 * The last chunk must contain at least one page plus the message
	 * buffer to avoid complicating other code (message buffer address
	 * calculation, etc.).
	 */
	msgbuf_size = (MSGBUF_SIZE + PHYSMAP_ALIGN_MASK) & ~PHYSMAP_ALIGN_MASK;

	while (phys_avail[pa_indx].phys_beg + PHYSMAP_ALIGN + msgbuf_size >=
	       phys_avail[pa_indx].phys_end) {
		physmem -= atop(phys_avail[pa_indx].phys_end -
				phys_avail[pa_indx].phys_beg);
		phys_avail[pa_indx].phys_beg = 0;
		phys_avail[pa_indx].phys_end = 0;
		--pa_indx;
	}

	Maxmem = atop(phys_avail[pa_indx].phys_end);

	/* Trim off space for the message buffer. */
	phys_avail[pa_indx].phys_end -= msgbuf_size;

	avail_end = phys_avail[pa_indx].phys_end;

	/* Map the message buffer. */
	for (off = 0; off < msgbuf_size; off += PAGE_SIZE) {
		pmap_kenter((vm_offset_t)msgbufp + off, avail_end + off);
	}

	/*
	 * Try to get EFI framebuffer working as early as possible.
	 *
	 * WARN: Some BIOSes do not list the EFI framebuffer memory, causing
	 * the pmap probe code to create a DMAP that does not cover its
	 * physical address space, efi_fb_init_vaddr(1) might not return
	 * an initialized framebuffer base pointer.  In this situation the
	 * later efi_fb_init_vaddr(0) call will deal with it.
	 */
	if (have_efi_framebuffer)
		efi_fb_init_vaddr(1);
}

struct machintr_abi MachIntrABI;

/*
 * IDT VECTORS:
 *	0	Divide by zero
 *	1	Debug
 *	2	NMI
 *	3	BreakPoint
 *	4	OverFlow
 *	5	Bound-Range
 *	6	Invalid OpCode
 *	7	Device Not Available (x87)
 *	8	Double-Fault
 *	9	Coprocessor Segment overrun (unsupported, reserved)
 *	10	Invalid-TSS
 *	11	Segment not present
 *	12	Stack
 *	13	General Protection
 *	14	Page Fault
 *	15	Reserved
 *	16	x87 FP Exception pending
 *	17	Alignment Check
 *	18	Machine Check
 *	19	SIMD floating point
 *	20-31	reserved
 *	32-255	INTn/external sources
 */
u_int64_t
hammer_time(u_int64_t modulep, u_int64_t physfree)
{
	caddr_t kmdp;
	int gsel_tss, x, cpu;
#if 0 /* JG */
	int metadata_missing, off;
#endif
	struct mdglobaldata *gd;
	struct privatespace *ps;
	u_int64_t msr;

	/*
	 * Prevent lowering of the ipl if we call tsleep() early.
	 */
	gd = &CPU_prvspace[0]->mdglobaldata;
	ps = (struct privatespace *)gd;
	bzero(gd, sizeof(*gd));
	bzero(&ps->common_tss, sizeof(ps->common_tss));

	/*
	 * Note: on both UP and SMP curthread must be set non-NULL
	 * early in the boot sequence because the system assumes
	 * that 'curthread' is never NULL.
	 */

	gd->mi.gd_curthread = &thread0;
	thread0.td_gd = &gd->mi;

	atdevbase = ISA_HOLE_START + PTOV_OFFSET;

#if 0 /* JG */
	metadata_missing = 0;
	if (bootinfo.bi_modulep) {
		preload_metadata = (caddr_t)bootinfo.bi_modulep + KERNBASE;
		preload_bootstrap_relocate(KERNBASE);
	} else {
		metadata_missing = 1;
	}
	if (bootinfo.bi_envp)
		kern_envp = (caddr_t)bootinfo.bi_envp + KERNBASE;
#endif

	preload_metadata = (caddr_t)(uintptr_t)(modulep + PTOV_OFFSET);
	preload_bootstrap_relocate(PTOV_OFFSET);
	kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type("elf64 kernel");
	boothowto = MD_FETCH(kmdp, MODINFOMD_HOWTO, int);
	kern_envp = MD_FETCH(kmdp, MODINFOMD_ENVP, char *) + PTOV_OFFSET;
#ifdef DDB
	ksym_start = MD_FETCH(kmdp, MODINFOMD_SSYM, uintptr_t);
	ksym_end = MD_FETCH(kmdp, MODINFOMD_ESYM, uintptr_t);
#endif
	efi_systbl_phys = MD_FETCH(kmdp, MODINFOMD_FW_HANDLE, vm_paddr_t);

	if (boothowto & RB_VERBOSE)
		bootverbose++;

	/*
	 * Default MachIntrABI to ICU
	 */
	MachIntrABI = MachIntrABI_ICU;

	/*
	 * start with one cpu.  Note: with one cpu, ncpus_fit_mask remain 0.
	 */
	ncpus = 1;
	ncpus_fit = 1;
	/* Init basic tunables, hz etc */
	init_param1();

	/*
	 * make gdt memory segments
	 */
	gdt_segs[GPROC0_SEL].ssd_base =
		(uintptr_t) &CPU_prvspace[0]->common_tss;

	gd->mi.gd_prvspace = CPU_prvspace[0];

	for (x = 0; x < NGDT; x++) {
		if (x != GPROC0_SEL && x != (GPROC0_SEL + 1))
			ssdtosd(&gdt_segs[x], &gdt[x]);
	}
	ssdtosyssd(&gdt_segs[GPROC0_SEL],
	    (struct system_segment_descriptor *)&gdt[GPROC0_SEL]);

	r_gdt.rd_limit = NGDT * sizeof(gdt[0]) - 1;
	r_gdt.rd_base =  (long) gdt;
	lgdt(&r_gdt);

	wrmsr(MSR_FSBASE, 0);		/* User value */
	wrmsr(MSR_GSBASE, (u_int64_t)&gd->mi);
	wrmsr(MSR_KGSBASE, 0);		/* User value while in the kernel */

	mi_gdinit(&gd->mi, 0);
	cpu_gdinit(gd, 0);
	proc0paddr = proc0paddr_buff;
	mi_proc0init(&gd->mi, proc0paddr);
	safepri = TDPRI_MAX;

	/* spinlocks and the BGL */
	init_locks();

	/* exceptions */
	for (x = 0; x < NIDT; x++)
		setidt_global(x, rsvdary[x], SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_DE, &IDTVEC(div),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_DB, &IDTVEC(dbg),  SDT_SYSIGT, SEL_KPL, 2);
	setidt_global(IDT_NMI, &IDTVEC(nmi),  SDT_SYSIGT, SEL_KPL, 1);
	setidt_global(IDT_BP, &IDTVEC(bpt),  SDT_SYSIGT, SEL_UPL, 0);
	setidt_global(IDT_OF, &IDTVEC(ofl),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_BR, &IDTVEC(bnd),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_UD, &IDTVEC(ill),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_NM, &IDTVEC(dna),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_DF, &IDTVEC(dblfault), SDT_SYSIGT, SEL_KPL, 1);
	setidt_global(IDT_FPUGP, &IDTVEC(fpusegm),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_TS, &IDTVEC(tss),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_NP, &IDTVEC(missing),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_SS, &IDTVEC(stk),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_GP, &IDTVEC(prot),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_PF, &IDTVEC(page),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_MF, &IDTVEC(fpu),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_AC, &IDTVEC(align), SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_MC, &IDTVEC(mchk),  SDT_SYSIGT, SEL_KPL, 0);
	setidt_global(IDT_XF, &IDTVEC(xmm), SDT_SYSIGT, SEL_KPL, 0);

	for (cpu = 0; cpu < MAXCPU; ++cpu) {
		r_idt_arr[cpu].rd_limit = sizeof(idt_arr[cpu]) - 1;
		r_idt_arr[cpu].rd_base = (long) &idt_arr[cpu][0];
	}

	lidt(&r_idt_arr[0]);

	/*
	 * Initialize the console before we print anything out.
	 */
	cninit();

#if 0 /* JG */
	if (metadata_missing)
		kprintf("WARNING: loader(8) metadata is missing!\n");
#endif

#if	NISA >0
	elcr_probe();
	isa_defaultirq();
#endif
	rand_initialize();

	/*
	 * Initialize IRQ mapping
	 *
	 * NOTE:
	 * SHOULD be after elcr_probe()
	 */
	MachIntrABI_ICU.initmap();
	MachIntrABI_IOAPIC.initmap();

#ifdef DDB
	kdb_init();
	if (boothowto & RB_KDB)
		Debugger("Boot flags requested debugger");
#endif

	identify_cpu();		/* Final stage of CPU initialization */
	initializecpu(0);	/* Initialize CPU registers */

	/*
	 * On modern Intel cpus, haswell or later, cpu_idle_hlt=1 is better
	 * because the cpu does significant power management in MWAIT
	 * (also suggested is to set sysctl machdep.mwait.CX.idle=AUTODEEP).
	 *
	 * On many AMD cpus cpu_idle_hlt=3 is better, because the cpu does
	 * significant power management only when using ACPI halt mode.
	 * (However, on Ryzen, mode 4 (HLT) also does power management).
	 *
	 * On older AMD or Intel cpus, cpu_idle_hlt=2 is better because ACPI
	 * is needed to reduce power consumption, but wakeup times are often
	 * too long.
	 */
	if (cpu_vendor_id == CPU_VENDOR_INTEL &&
	    CPUID_TO_MODEL(cpu_id) >= 0x3C) {	/* Haswell or later */
		cpu_idle_hlt = 1;
	}
	if (cpu_vendor_id == CPU_VENDOR_AMD) {
		if (CPUID_TO_FAMILY(cpu_id) >= 0x17) {
			/* Ryzen or later */
			cpu_idle_hlt = 3;
		} else if (CPUID_TO_FAMILY(cpu_id) >= 0x14) {
			/* Bobcat or later */
			cpu_idle_hlt = 3;
		}
	}

	TUNABLE_INT_FETCH("hw.apic_io_enable", &ioapic_enable); /* for compat */
	TUNABLE_INT_FETCH("hw.ioapic_enable", &ioapic_enable);
	TUNABLE_INT_FETCH("hw.lapic_enable", &lapic_enable);
	TUNABLE_INT_FETCH("machdep.cpu_idle_hlt", &cpu_idle_hlt);

	/*
	 * By default always enable the ioapic.  Certain virtual machines
	 * may not work with the I/O apic enabled and can be specified in
	 * the case statement below.  On the other hand, if the ioapic is
	 * disabled for virtual machines which DO work with the I/O apic,
	 * the virtual machine can implode if we disable the I/O apic.
	 *
	 * For now enable the ioapic for all guests.
	 *
	 * NOTE: This must be done after identify_cpu(), which sets
	 *	 'cpu_feature2'.
	 */
	if (ioapic_enable < 0) {
		ioapic_enable = 1;
		switch(vmm_guest) {
		case VMM_GUEST_NONE:	/* should be enabled on real HW */
		case VMM_GUEST_KVM:	/* must be enabled or VM implodes */
			ioapic_enable = 1;
			break;
		default:		/* enable by default for other VMs */
			ioapic_enable = 1;
			break;
		}
	}

	/*
	 * TSS entry point for interrupts, traps, and exceptions
	 * (sans NMI).  This will always go to near the top of the pcpu
	 * trampoline area.  Hardware-pushed data will be copied into
	 * the trap-frame on entry, and (if necessary) returned to the
	 * trampoline on exit.
	 *
	 * We store some pcb data for the trampoline code above the
	 * stack the cpu hw pushes into, and arrange things so the
	 * address of tr_pcb_rsp is the same as the desired top of
	 * stack.
	 */
	ps->common_tss.tss_rsp0 = (register_t)&ps->trampoline.tr_pcb_rsp;
	ps->trampoline.tr_pcb_rsp = ps->common_tss.tss_rsp0;
	ps->trampoline.tr_pcb_gs_kernel = (register_t)gd;
	ps->trampoline.tr_pcb_cr3 = KPML4phys;	/* adj to user cr3 live */
	ps->dbltramp.tr_pcb_gs_kernel = (register_t)gd;
	ps->dbltramp.tr_pcb_cr3 = KPML4phys;
	ps->dbgtramp.tr_pcb_gs_kernel = (register_t)gd;
	ps->dbgtramp.tr_pcb_cr3 = KPML4phys;

	/* double fault stack */
	ps->common_tss.tss_ist1 = (register_t)&ps->dbltramp.tr_pcb_rsp;
	/* #DB debugger needs its own stack */
	ps->common_tss.tss_ist2 = (register_t)&ps->dbgtramp.tr_pcb_rsp;

	/* Set the IO permission bitmap (empty due to tss seg limit) */
	ps->common_tss.tss_iobase = sizeof(struct x86_64tss);

	gsel_tss = GSEL(GPROC0_SEL, SEL_KPL);
	gd->gd_tss_gdt = &gdt[GPROC0_SEL];
	gd->gd_common_tssd = *gd->gd_tss_gdt;
	ltr(gsel_tss);

	/* Set up the fast syscall stuff */
	msr = rdmsr(MSR_EFER) | EFER_SCE;
	wrmsr(MSR_EFER, msr);
	wrmsr(MSR_LSTAR, (u_int64_t)IDTVEC(fast_syscall));
	wrmsr(MSR_CSTAR, (u_int64_t)IDTVEC(fast_syscall32));
	msr = ((u_int64_t)GSEL(GCODE_SEL, SEL_KPL) << 32) |
	      ((u_int64_t)GSEL(GUCODE32_SEL, SEL_UPL) << 48);
	wrmsr(MSR_STAR, msr);
	wrmsr(MSR_SF_MASK, PSL_NT|PSL_T|PSL_I|PSL_C|PSL_D|PSL_IOPL|PSL_AC);

	getmemsize(kmdp, physfree);
	init_param2(physmem);

	/* now running on new page tables, configured,and u/iom is accessible */

	/* Map the message buffer. */
#if 0 /* JG */
	for (off = 0; off < round_page(MSGBUF_SIZE); off += PAGE_SIZE)
		pmap_kenter((vm_offset_t)msgbufp + off, avail_end + off);
#endif

	msgbufinit(msgbufp, MSGBUF_SIZE);


	/* transfer to user mode */

	_ucodesel = GSEL(GUCODE_SEL, SEL_UPL);
	_udatasel = GSEL(GUDATA_SEL, SEL_UPL);
	_ucode32sel = GSEL(GUCODE32_SEL, SEL_UPL);

	load_ds(_udatasel);
	load_es(_udatasel);
	load_fs(_udatasel);

	/* setup proc 0's pcb */
	thread0.td_pcb->pcb_flags = 0;
	thread0.td_pcb->pcb_cr3 = KPML4phys;
	thread0.td_pcb->pcb_cr3_iso = 0;
	thread0.td_pcb->pcb_ext = NULL;
	lwp0.lwp_md.md_regs = &proc0_tf;	/* XXX needed? */

	/* Location of kernel stack for locore */
	return ((u_int64_t)thread0.td_pcb);
}

/*
 * Initialize machine-dependant portions of the global data structure.
 * Note that the global data area and cpu0's idlestack in the private
 * data space were allocated in locore.
 *
 * Note: the idlethread's cpl is 0
 *
 * WARNING!  Called from early boot, 'mycpu' may not work yet.
 */
void
cpu_gdinit(struct mdglobaldata *gd, int cpu)
{
	if (cpu)
		gd->mi.gd_curthread = &gd->mi.gd_idlethread;

	lwkt_init_thread(&gd->mi.gd_idlethread,
			gd->mi.gd_prvspace->idlestack,
			sizeof(gd->mi.gd_prvspace->idlestack),
			0, &gd->mi);
	lwkt_set_comm(&gd->mi.gd_idlethread, "idle_%d", cpu);
	gd->mi.gd_idlethread.td_switch = cpu_lwkt_switch;
	gd->mi.gd_idlethread.td_sp -= sizeof(void *);
	*(void **)gd->mi.gd_idlethread.td_sp = cpu_idle_restore;
}

/*
 * We only have to check for DMAP bounds, the globaldata space is
 * actually part of the kernel_map so we don't have to waste time
 * checking CPU_prvspace[*].
 */
int
is_globaldata_space(vm_offset_t saddr, vm_offset_t eaddr)
{
#if 0
	if (saddr >= (vm_offset_t)&CPU_prvspace[0] &&
	    eaddr <= (vm_offset_t)&CPU_prvspace[MAXCPU]) {
		return (TRUE);
	}
#endif
	if (saddr >= DMAP_MIN_ADDRESS && eaddr <= DMAP_MAX_ADDRESS)
		return (TRUE);
	return (FALSE);
}

struct globaldata *
globaldata_find(int cpu)
{
	KKASSERT(cpu >= 0 && cpu < ncpus);
	return(&CPU_prvspace[cpu]->mdglobaldata.mi);
}

/*
 * This path should be safe from the SYSRET issue because only stopped threads
 * can have their %rip adjusted this way (and all heavy weight thread switches
 * clear QUICKREF and thus do not use SYSRET).  However, the code path is
 * convoluted so add a safety by forcing %rip to be cannonical.
 */
int
ptrace_set_pc(struct lwp *lp, unsigned long addr)
{
	if (addr & 0x0000800000000000LLU)
		lp->lwp_md.md_regs->tf_rip = addr | 0xFFFF000000000000LLU;
	else
		lp->lwp_md.md_regs->tf_rip = addr & 0x0000FFFFFFFFFFFFLLU;
	return (0);
}

int
ptrace_single_step(struct lwp *lp)
{
	lp->lwp_md.md_regs->tf_rflags |= PSL_T;
	return (0);
}

int
fill_regs(struct lwp *lp, struct reg *regs)
{
	struct trapframe *tp;

	if ((tp = lp->lwp_md.md_regs) == NULL)
		return EINVAL;
	bcopy(&tp->tf_rdi, &regs->r_rdi, sizeof(*regs));
	return (0);
}

int
set_regs(struct lwp *lp, struct reg *regs)
{
	struct trapframe *tp;

	tp = lp->lwp_md.md_regs;
	if (!EFL_SECURE(regs->r_rflags, tp->tf_rflags) ||
	    !CS_SECURE(regs->r_cs))
		return (EINVAL);
	bcopy(&regs->r_rdi, &tp->tf_rdi, sizeof(*regs));
	clear_quickret();
	return (0);
}

static void
fill_fpregs_xmm(struct savexmm *sv_xmm, struct save87 *sv_87)
{
	struct env87 *penv_87 = &sv_87->sv_env;
	struct envxmm *penv_xmm = &sv_xmm->sv_env;
	int i;

	/* FPU control/status */
	penv_87->en_cw = penv_xmm->en_cw;
	penv_87->en_sw = penv_xmm->en_sw;
	penv_87->en_tw = penv_xmm->en_tw;
	penv_87->en_fip = penv_xmm->en_fip;
	penv_87->en_fcs = penv_xmm->en_fcs;
	penv_87->en_opcode = penv_xmm->en_opcode;
	penv_87->en_foo = penv_xmm->en_foo;
	penv_87->en_fos = penv_xmm->en_fos;

	/* FPU registers */
	for (i = 0; i < 8; ++i)
		sv_87->sv_ac[i] = sv_xmm->sv_fp[i].fp_acc;
}

static void
set_fpregs_xmm(struct save87 *sv_87, struct savexmm *sv_xmm)
{
	struct env87 *penv_87 = &sv_87->sv_env;
	struct envxmm *penv_xmm = &sv_xmm->sv_env;
	int i;

	/* FPU control/status */
	penv_xmm->en_cw = penv_87->en_cw;
	penv_xmm->en_sw = penv_87->en_sw;
	penv_xmm->en_tw = penv_87->en_tw;
	penv_xmm->en_fip = penv_87->en_fip;
	penv_xmm->en_fcs = penv_87->en_fcs;
	penv_xmm->en_opcode = penv_87->en_opcode;
	penv_xmm->en_foo = penv_87->en_foo;
	penv_xmm->en_fos = penv_87->en_fos;

	/* FPU registers */
	for (i = 0; i < 8; ++i)
		sv_xmm->sv_fp[i].fp_acc = sv_87->sv_ac[i];
}

int
fill_fpregs(struct lwp *lp, struct fpreg *fpregs)
{
	if (lp->lwp_thread == NULL || lp->lwp_thread->td_pcb == NULL)
		return EINVAL;
	if (cpu_fxsr) {
		fill_fpregs_xmm(&lp->lwp_thread->td_pcb->pcb_save.sv_xmm,
				(struct save87 *)fpregs);
		return (0);
	}
	bcopy(&lp->lwp_thread->td_pcb->pcb_save.sv_87, fpregs, sizeof *fpregs);
	return (0);
}

int
set_fpregs(struct lwp *lp, struct fpreg *fpregs)
{
	if (cpu_fxsr) {
		set_fpregs_xmm((struct save87 *)fpregs,
			       &lp->lwp_thread->td_pcb->pcb_save.sv_xmm);
		return (0);
	}
	bcopy(fpregs, &lp->lwp_thread->td_pcb->pcb_save.sv_87, sizeof *fpregs);
	return (0);
}

int
fill_dbregs(struct lwp *lp, struct dbreg *dbregs)
{
	struct pcb *pcb;

	if (lp == NULL) {
		dbregs->dr[0] = rdr0();
		dbregs->dr[1] = rdr1();
		dbregs->dr[2] = rdr2();
		dbregs->dr[3] = rdr3();
		dbregs->dr[4] = rdr4();
		dbregs->dr[5] = rdr5();
		dbregs->dr[6] = rdr6();
		dbregs->dr[7] = rdr7();
		return (0);
	}
	if (lp->lwp_thread == NULL || (pcb = lp->lwp_thread->td_pcb) == NULL)
		return EINVAL;
	dbregs->dr[0] = pcb->pcb_dr0;
	dbregs->dr[1] = pcb->pcb_dr1;
	dbregs->dr[2] = pcb->pcb_dr2;
	dbregs->dr[3] = pcb->pcb_dr3;
	dbregs->dr[4] = 0;
	dbregs->dr[5] = 0;
	dbregs->dr[6] = pcb->pcb_dr6;
	dbregs->dr[7] = pcb->pcb_dr7;
	return (0);
}

int
set_dbregs(struct lwp *lp, struct dbreg *dbregs)
{
	if (lp == NULL) {
		load_dr0(dbregs->dr[0]);
		load_dr1(dbregs->dr[1]);
		load_dr2(dbregs->dr[2]);
		load_dr3(dbregs->dr[3]);
		load_dr4(dbregs->dr[4]);
		load_dr5(dbregs->dr[5]);
		load_dr6(dbregs->dr[6]);
		load_dr7(dbregs->dr[7]);
	} else {
		struct pcb *pcb;
		struct ucred *ucred;
		int i;
		uint64_t mask1, mask2;

		/*
		 * Don't let an illegal value for dr7 get set.	Specifically,
		 * check for undefined settings.  Setting these bit patterns
		 * result in undefined behaviour and can lead to an unexpected
		 * TRCTRAP.
		 */
		/* JG this loop looks unreadable */
		/* Check 4 2-bit fields for invalid patterns.
		 * These fields are R/Wi, for i = 0..3
		 */
		/* Is 10 in LENi allowed when running in compatibility mode? */
		/* Pattern 10 in R/Wi might be used to indicate
		 * breakpoint on I/O. Further analysis should be
		 * carried to decide if it is safe and useful to
		 * provide access to that capability
		 */
		for (i = 0, mask1 = 0x3<<16, mask2 = 0x2<<16; i < 4;
		     i++, mask1 <<= 4, mask2 <<= 4)
			if ((dbregs->dr[7] & mask1) == mask2)
				return (EINVAL);

		pcb = lp->lwp_thread->td_pcb;
		ucred = lp->lwp_proc->p_ucred;

		/*
		 * Don't let a process set a breakpoint that is not within the
		 * process's address space.  If a process could do this, it
		 * could halt the system by setting a breakpoint in the kernel
		 * (if ddb was enabled).  Thus, we need to check to make sure
		 * that no breakpoints are being enabled for addresses outside
		 * process's address space, unless, perhaps, we were called by
		 * uid 0.
		 *
		 * XXX - what about when the watched area of the user's
		 * address space is written into from within the kernel
		 * ... wouldn't that still cause a breakpoint to be generated
		 * from within kernel mode?
		 */

		if (priv_check_cred(ucred, PRIV_ROOT, 0) != 0) {
			if (dbregs->dr[7] & 0x3) {
				/* dr0 is enabled */
				if (dbregs->dr[0] >= VM_MAX_USER_ADDRESS)
					return (EINVAL);
			}

			if (dbregs->dr[7] & (0x3<<2)) {
				/* dr1 is enabled */
				if (dbregs->dr[1] >= VM_MAX_USER_ADDRESS)
					return (EINVAL);
			}

			if (dbregs->dr[7] & (0x3<<4)) {
				/* dr2 is enabled */
				if (dbregs->dr[2] >= VM_MAX_USER_ADDRESS)
					return (EINVAL);
			}

			if (dbregs->dr[7] & (0x3<<6)) {
				/* dr3 is enabled */
				if (dbregs->dr[3] >= VM_MAX_USER_ADDRESS)
					return (EINVAL);
			}
		}

		pcb->pcb_dr0 = dbregs->dr[0];
		pcb->pcb_dr1 = dbregs->dr[1];
		pcb->pcb_dr2 = dbregs->dr[2];
		pcb->pcb_dr3 = dbregs->dr[3];
		pcb->pcb_dr6 = dbregs->dr[6];
		pcb->pcb_dr7 = dbregs->dr[7];

		pcb->pcb_flags |= PCB_DBREGS;
	}

	return (0);
}

/*
 * Return > 0 if a hardware breakpoint has been hit, and the
 * breakpoint was in user space.  Return 0, otherwise.
 */
int
user_dbreg_trap(void)
{
	u_int64_t dr7, dr6; /* debug registers dr6 and dr7 */
	u_int64_t bp;       /* breakpoint bits extracted from dr6 */
	int nbp;            /* number of breakpoints that triggered */
	caddr_t addr[4];    /* breakpoint addresses */
	int i;

	dr7 = rdr7();
	if ((dr7 & 0xff) == 0) {
		/*
		 * all GE and LE bits in the dr7 register are zero,
		 * thus the trap couldn't have been caused by the
		 * hardware debug registers
		 */
		return 0;
	}

	nbp = 0;
	dr6 = rdr6();
	bp = dr6 & 0xf;

	if (bp == 0) {
		/*
		 * None of the breakpoint bits are set meaning this
		 * trap was not caused by any of the debug registers
		 */
		return 0;
	}

	/*
	 * at least one of the breakpoints were hit, check to see
	 * which ones and if any of them are user space addresses
	 */

	if (bp & 0x01) {
		addr[nbp++] = (caddr_t)rdr0();
	}
	if (bp & 0x02) {
		addr[nbp++] = (caddr_t)rdr1();
	}
	if (bp & 0x04) {
		addr[nbp++] = (caddr_t)rdr2();
	}
	if (bp & 0x08) {
		addr[nbp++] = (caddr_t)rdr3();
	}

	for (i = 0; i < nbp; i++) {
		if (addr[i] < (caddr_t)VM_MAX_USER_ADDRESS) {
			/*
			 * addr[i] is in user space
			 */
			return nbp;
		}
	}

	/*
	 * None of the breakpoints are in user space.
	 */
	return 0;
}


#ifndef DDB
void
Debugger(const char *msg)
{
	kprintf("Debugger(\"%s\") called.\n", msg);
}
#endif /* no DDB */

#ifdef DDB

/*
 * Provide inb() and outb() as functions.  They are normally only
 * available as macros calling inlined functions, thus cannot be
 * called inside DDB.
 *
 * The actual code is stolen from <machine/cpufunc.h>, and de-inlined.
 */

#undef inb
#undef outb

/* silence compiler warnings */
u_char inb(u_int);
void outb(u_int, u_char);

u_char
inb(u_int port)
{
	u_char	data;
	/*
	 * We use %%dx and not %1 here because i/o is done at %dx and not at
	 * %edx, while gcc generates inferior code (movw instead of movl)
	 * if we tell it to load (u_short) port.
	 */
	__asm __volatile("inb %%dx,%0" : "=a" (data) : "d" (port));
	return (data);
}

void
outb(u_int port, u_char data)
{
	u_char	al;
	/*
	 * Use an unnecessary assignment to help gcc's register allocator.
	 * This make a large difference for gcc-1.40 and a tiny difference
	 * for gcc-2.6.0.  For gcc-1.40, al had to be ``asm("ax")'' for
	 * best results.  gcc-2.6.0 can't handle this.
	 */
	al = data;
	__asm __volatile("outb %0,%%dx" : : "a" (al), "d" (port));
}

#endif /* DDB */



/*
 * initialize all the SMP locks
 */

/* critical region when masking or unmasking interupts */
struct spinlock_deprecated imen_spinlock;

/* locks com (tty) data/hardware accesses: a FASTINTR() */
struct spinlock_deprecated com_spinlock;

/* lock regions around the clock hardware */
struct spinlock_deprecated clock_spinlock;

static void
init_locks(void)
{
	/*
	 * Get the initial mplock with a count of 1 for the BSP.
	 * This uses a LOGICAL cpu ID, ie BSP == 0.
	 */
	cpu_get_initial_mplock();
	/* DEPRECATED */
	spin_init_deprecated(&imen_spinlock);
	spin_init_deprecated(&com_spinlock);
	spin_init_deprecated(&clock_spinlock);

	/* our token pool needs to work early */
	lwkt_token_pool_init();
}

boolean_t
cpu_mwait_hint_valid(uint32_t hint)
{
	int cx_idx, sub;

	cx_idx = MWAIT_EAX_TO_CX(hint);
	if (cx_idx >= CPU_MWAIT_CX_MAX)
		return FALSE;

	sub = MWAIT_EAX_TO_CX_SUB(hint);
	if (sub >= cpu_mwait_cx_info[cx_idx].subcnt)
		return FALSE;

	return TRUE;
}

void
cpu_mwait_cx_no_bmsts(void)
{
	atomic_clear_int(&cpu_mwait_c3_preamble, CPU_MWAIT_C3_PREAMBLE_BM_STS);
}

void
cpu_mwait_cx_no_bmarb(void)
{
	atomic_clear_int(&cpu_mwait_c3_preamble, CPU_MWAIT_C3_PREAMBLE_BM_ARB);
}

static int
cpu_mwait_cx_hint2name(int hint, char *name, int namelen, boolean_t allow_auto)
{
	int old_cx_idx, sub = 0;

	if (hint >= 0) {
		old_cx_idx = MWAIT_EAX_TO_CX(hint);
		sub = MWAIT_EAX_TO_CX_SUB(hint);
	} else if (hint == CPU_MWAIT_HINT_AUTO) {
		old_cx_idx = allow_auto ? CPU_MWAIT_C2 : CPU_MWAIT_CX_MAX;
	} else if (hint == CPU_MWAIT_HINT_AUTODEEP) {
		old_cx_idx = allow_auto ? CPU_MWAIT_C3 : CPU_MWAIT_CX_MAX;
	} else {
		old_cx_idx = CPU_MWAIT_CX_MAX;
	}

	if (!CPU_MWAIT_HAS_CX)
		strlcpy(name, "NONE", namelen);
	else if (allow_auto && hint == CPU_MWAIT_HINT_AUTO)
		strlcpy(name, "AUTO", namelen);
	else if (allow_auto && hint == CPU_MWAIT_HINT_AUTODEEP)
		strlcpy(name, "AUTODEEP", namelen);
	else if (old_cx_idx >= CPU_MWAIT_CX_MAX ||
	    sub >= cpu_mwait_cx_info[old_cx_idx].subcnt)
		strlcpy(name, "INVALID", namelen);
	else
		ksnprintf(name, namelen, "C%d/%d", old_cx_idx, sub);

	return old_cx_idx;
}

static int
cpu_mwait_cx_name2hint(char *name, int *hint0, boolean_t allow_auto)
{
	int cx_idx, sub, hint;
	char *ptr, *start;

	if (allow_auto && strcmp(name, "AUTO") == 0) {
		hint = CPU_MWAIT_HINT_AUTO;
		cx_idx = CPU_MWAIT_C2;
		goto done;
	}
	if (allow_auto && strcmp(name, "AUTODEEP") == 0) {
		hint = CPU_MWAIT_HINT_AUTODEEP;
		cx_idx = CPU_MWAIT_C3;
		goto done;
	}

	if (strlen(name) < 4 || toupper(name[0]) != 'C')
		return -1;
	start = &name[1];
	ptr = NULL;

	cx_idx = strtol(start, &ptr, 10);
	if (ptr == start || *ptr != '/')
		return -1;
	if (cx_idx < 0 || cx_idx >= CPU_MWAIT_CX_MAX)
		return -1;

	start = ptr + 1;
	ptr = NULL;

	sub = strtol(start, &ptr, 10);
	if (*ptr != '\0')
		return -1;
	if (sub < 0 || sub >= cpu_mwait_cx_info[cx_idx].subcnt)
		return -1;

	hint = MWAIT_EAX_HINT(cx_idx, sub);
done:
	*hint0 = hint;
	return cx_idx;
}

static int
cpu_mwait_cx_transit(int old_cx_idx, int cx_idx)
{
	if (cx_idx >= CPU_MWAIT_C3 && cpu_mwait_c3_preamble)
		return EOPNOTSUPP;
	if (old_cx_idx < CPU_MWAIT_C3 && cx_idx >= CPU_MWAIT_C3) {
		int error;

		error = cputimer_intr_powersave_addreq();
		if (error)
			return error;
	} else if (old_cx_idx >= CPU_MWAIT_C3 && cx_idx < CPU_MWAIT_C3) {
		cputimer_intr_powersave_remreq();
	}
	return 0;
}

static int
cpu_mwait_cx_select_sysctl(SYSCTL_HANDLER_ARGS, int *hint0,
    boolean_t allow_auto)
{
	int error, cx_idx, old_cx_idx, hint;
	char name[CPU_MWAIT_CX_NAMELEN];

	hint = *hint0;
	old_cx_idx = cpu_mwait_cx_hint2name(hint, name, sizeof(name),
	    allow_auto);

	error = sysctl_handle_string(oidp, name, sizeof(name), req);
	if (error != 0 || req->newptr == NULL)
		return error;

	if (!CPU_MWAIT_HAS_CX)
		return EOPNOTSUPP;

	cx_idx = cpu_mwait_cx_name2hint(name, &hint, allow_auto);
	if (cx_idx < 0)
		return EINVAL;

	error = cpu_mwait_cx_transit(old_cx_idx, cx_idx);
	if (error)
		return error;

	*hint0 = hint;
	return 0;
}

static int
cpu_mwait_cx_setname(struct cpu_idle_stat *stat, const char *cx_name)
{
	int error, cx_idx, old_cx_idx, hint;
	char name[CPU_MWAIT_CX_NAMELEN];

	KASSERT(CPU_MWAIT_HAS_CX, ("cpu does not support mwait CX extension"));

	hint = stat->hint;
	old_cx_idx = cpu_mwait_cx_hint2name(hint, name, sizeof(name), TRUE);

	strlcpy(name, cx_name, sizeof(name));
	cx_idx = cpu_mwait_cx_name2hint(name, &hint, TRUE);
	if (cx_idx < 0)
		return EINVAL;

	error = cpu_mwait_cx_transit(old_cx_idx, cx_idx);
	if (error)
		return error;

	stat->hint = hint;
	return 0;
}

static int
cpu_mwait_cx_idle_sysctl(SYSCTL_HANDLER_ARGS)
{
	int hint = cpu_mwait_halt_global;
	int error, cx_idx, cpu;
	char name[CPU_MWAIT_CX_NAMELEN], cx_name[CPU_MWAIT_CX_NAMELEN];

	cpu_mwait_cx_hint2name(hint, name, sizeof(name), TRUE);

	error = sysctl_handle_string(oidp, name, sizeof(name), req);
	if (error != 0 || req->newptr == NULL)
		return error;

	if (!CPU_MWAIT_HAS_CX)
		return EOPNOTSUPP;

	/* Save name for later per-cpu CX configuration */
	strlcpy(cx_name, name, sizeof(cx_name));

	cx_idx = cpu_mwait_cx_name2hint(name, &hint, TRUE);
	if (cx_idx < 0)
		return EINVAL;

	/* Change per-cpu CX configuration */
	for (cpu = 0; cpu < ncpus; ++cpu) {
		error = cpu_mwait_cx_setname(&cpu_idle_stats[cpu], cx_name);
		if (error)
			return error;
	}

	cpu_mwait_halt_global = hint;
	return 0;
}

static int
cpu_mwait_cx_pcpu_idle_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct cpu_idle_stat *stat = arg1;
	int error;

	error = cpu_mwait_cx_select_sysctl(oidp, arg1, arg2, req,
	    &stat->hint, TRUE);
	return error;
}

static int
cpu_mwait_cx_spin_sysctl(SYSCTL_HANDLER_ARGS)
{
	int error;

	error = cpu_mwait_cx_select_sysctl(oidp, arg1, arg2, req,
	    &cpu_mwait_spin, FALSE);
	return error;
}

/*
 * This manual debugging code is called unconditionally from Xtimer
 * (the per-cpu timer interrupt) whether the current thread is in a
 * critical section or not) and can be useful in tracking down lockups.
 *
 * NOTE: MANUAL DEBUG CODE
 */
#if 0
static int saveticks[SMP_MAXCPU];
static int savecounts[SMP_MAXCPU];
#endif
static tsc_uclock_t last_tsc[SMP_MAXCPU];

void
pcpu_timer_always(struct intrframe *frame)
{
	globaldata_t gd;
	thread_t td;
	char *top;
	char *bot;
	char *rbp;
	char *rip;
	int n;
	tsc_uclock_t tsc;

	if (flame_poll_debug == 0)
		return;
	gd = mycpu;
	tsc = rdtsc() - last_tsc[gd->gd_cpuid];
	if (tsc_frequency == 0 || tsc < tsc_frequency)
		return;
	last_tsc[gd->gd_cpuid] = rdtsc();

	td = gd->gd_curthread;
	if (td == NULL)
		return;
	bot = (char *)td->td_kstack + PAGE_SIZE;        /* skip guard */
	top = (char *)td->td_kstack + td->td_kstack_size;
	if (bot >= top)
		return;

	rip = (char *)(intptr_t)frame->if_rip;
	kprintf("POLL%02d %016lx", gd->gd_cpuid, (intptr_t)rip);
	rbp = (char *)(intptr_t)frame->if_rbp;

	for (n = 1; n < 8; ++n) {
		if (rbp < bot || rbp > top - 8 || ((intptr_t)rbp & 7))
			break;
		kprintf("<-%016lx", (intptr_t)*(char **)(rbp + 8));
		if (*(char **)rbp <= rbp)
			break;
		rbp = *(char **)rbp;
	}
	kprintf("\n");
	cpu_sfence();
}

SET_DECLARE(smap_open, char);
SET_DECLARE(smap_close, char);

static void
cpu_implement_smap(void)
{
	char **scan;

	for (scan = SET_BEGIN(smap_open);		/* nop -> stac */
	     scan < SET_LIMIT(smap_open); ++scan) {
		(*scan)[0] = 0x0F;
		(*scan)[1] = 0x01;
		(*scan)[2] = 0xCB;
	}
	for (scan = SET_BEGIN(smap_close);		/* nop -> clac */
	     scan < SET_LIMIT(smap_close); ++scan) {
		(*scan)[0] = 0x0F;
		(*scan)[1] = 0x01;
		(*scan)[2] = 0xCA;
	}
}

/*
 * From a hard interrupt
 */
int
cpu_interrupt_running(struct thread *td)
{
	struct mdglobaldata *gd = mdcpu;

	if (clock_debug1 > 0) {
		--clock_debug1;
		kprintf("%d %016lx %016lx %016lx\n",
			((td->td_flags & TDF_INTTHREAD) != 0),
			gd->gd_ipending[0],
			gd->gd_ipending[1],
			gd->gd_ipending[2]);
		if (td->td_flags & TDF_CLKTHREAD) {
			kprintf("CLKTD %s PREEMPT %s\n",
				td->td_comm,
				(td->td_preempted ?
				 td->td_preempted->td_comm : ""));
		} else {
			kprintf("NORTD %s\n", td->td_comm);
		}
	}
	if ((td->td_flags & TDF_INTTHREAD) ||
	    gd->gd_ipending[0] ||
	    gd->gd_ipending[1] ||
	    gd->gd_ipending[2]) {
		return 1;
	} else {
		return 0;
	}
}
