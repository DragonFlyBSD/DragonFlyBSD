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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/systm.h>
#include <sys/globaldata.h>
#include <sys/proc.h>
#include <sys/mbuf.h>
#include <sys/machintr.h>
#include <sys/msgbuf.h>
#include <sys/diskslice.h>
#include <sys/ptrace.h>
#include <sys/kerneldump.h>
#include <sys/memrange.h>
#include <sys/sysmsg.h>
#include <sys/sysproto.h>
#include <sys/signal2.h>
#include <sys/reg.h>
#include <machine/cpumask.h>
#include <machine/smp.h>
#include <machine/md_var.h>
#include <machine/globaldata.h>
#include <machine/pcb.h>
#include <cpu/tls.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <net/if.h>
#include <net/if_var.h>
#include <netinet/if_ether.h>

/* procfs prototypes - avoid including procfs.h due to vop dependencies */
int procfs_read_regs(struct lwp *, struct reg *);
int procfs_write_regs(struct lwp *, struct reg *);
int procfs_read_fpregs(struct lwp *, struct fpreg *);
int procfs_write_fpregs(struct lwp *, struct fpreg *);
int procfs_read_dbregs(struct lwp *, struct dbreg *);
int procfs_write_dbregs(struct lwp *, struct dbreg *);

static int
md_strcmp(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return ((unsigned char)*a - (unsigned char)*b);
}

#define	ARM64_KERNBASE	0xffffff8000000000ULL
#define	ARM64_PHYSBASE	0x0000000040000000ULL
#define	ARM64_PTOTV_OFF	(ARM64_KERNBASE - ARM64_PHYSBASE)

typedef u_long vm_offset_t;

#define	PTE_BLOCK_NORMAL_FLAGS	0x705

#define	MODINFOMD_EFI_MAP	0x1004

struct efi_map_header {
	u_int64_t	memory_size;
	u_int64_t	descriptor_size;
	u_int32_t	descriptor_version;
};

struct efi_md {
	u_int32_t	type;
	u_int32_t	pad;
	u_int64_t	phys_start;
	u_int64_t	virt_start;
	u_int64_t	num_pages;
	u_int64_t	attribute;
};

#define	EFI_MD_TYPE_CONVENTIONAL	7

#define	ARM64_MAX_PHYSMEM_RANGES	16

struct arm64_phys_range {
	u_int64_t	start;
	u_int64_t	end;
};

static struct arm64_phys_range arm64_physmem[ARM64_MAX_PHYSMEM_RANGES];
static int arm64_physmem_count;

static u_int64_t arm64_boot_alloc_base;
static u_int64_t arm64_boot_alloc_end;
static u_int64_t arm64_boot_alloc_next;
static u_int64_t arm64_ttbr1_candidate;

static void uart_puts(const char *str);
static void uart_puthex(u_int64_t value);

static void
arm64_high_trampoline(void)
{
	volatile u_int64_t scratch = 0;

	scratch++;
	uart_puts("[arm64] high-va ok\r\n");
}

static void
arm64_zero_page(u_int64_t addr)
{
	volatile u_int64_t *p = (volatile u_int64_t *)(uintptr_t)addr;
	for (u_int64_t i = 0; i < (4096 / sizeof(u_int64_t)); i++)
		p[i] = 0;
}

static u_int64_t
arm64_boot_alloc(u_int64_t size, u_int64_t align)
{
	u_int64_t addr;

	if (align == 0)
		align = 4096;
	addr = roundup(arm64_boot_alloc_next, align);
	if (addr + size > arm64_boot_alloc_end)
		return (0);
	arm64_boot_alloc_next = addr + size;
	return (addr);
}

static void
arm64_pmap_bootstrap(struct arm64_phys_range *ranges, int count)
{
	u_int64_t best_size;
	int best;

	if (count == 0) {
		uart_puts("[arm64] pmap bootstrap: no ranges\r\n");
		return;
	}

	best = 0;
	best_size = ranges[0].end - ranges[0].start;
	for (int i = 1; i < count; i++) {
		u_int64_t size = ranges[i].end - ranges[i].start;
		if (size > best_size) {
			best = i;
			best_size = size;
		}
	}

	uart_puts("[arm64] pmap bootstrap: largest range 0x");
	uart_puthex(ranges[best].start);
	uart_puts("-0x");
	uart_puthex(ranges[best].end);
	uart_puts("\r\n");

	arm64_boot_alloc_base = roundup(ranges[best].start, 4096);
	arm64_boot_alloc_end = ranges[best].end;
	arm64_boot_alloc_next = arm64_boot_alloc_base;

	uart_puts("[arm64] boot_alloc range 0x");
	uart_puthex(arm64_boot_alloc_base);
	uart_puts("-0x");
	uart_puthex(arm64_boot_alloc_end);
	uart_puts("\r\n");

	u_int64_t scratch = arm64_boot_alloc(2 * 4096, 4096);
	if (scratch != 0) {
		uart_puts("[arm64] boot_alloc scratch 0x");
		uart_puthex(scratch);
		uart_puts("-0x");
		uart_puthex(scratch + (2 * 4096));
		uart_puts("\r\n");
	} else {
		uart_puts("[arm64] boot_alloc scratch failed\r\n");
	}

	u_int64_t pt = arm64_boot_alloc(2 * 4096, 4096);
	if (pt != 0) {
		uart_puts("[arm64] boot_alloc pt 0x");
		uart_puthex(pt);
		uart_puts("-0x");
		uart_puthex(pt + (2 * 4096));
		uart_puts("\r\n");

		arm64_zero_page(pt);
		arm64_zero_page(pt + 4096);
		((u_int64_t *)(uintptr_t)pt)[0] =
		    ((pt + 4096) & ~0xfffULL) | 0x3;
		((u_int64_t *)(uintptr_t)pt)[511] =
		    ((pt + 4096) & ~0xfffULL) | 0x3;

		u_int64_t l2 = arm64_boot_alloc(4096, 4096);
		if (l2 != 0) {
			arm64_zero_page(l2);
			((u_int64_t *)(uintptr_t)(pt + 4096))[0] =
			    (l2 & ~0xfffULL) | 0x3;
			((u_int64_t *)(uintptr_t)l2)[0] =
			    ARM64_PHYSBASE | PTE_BLOCK_NORMAL_FLAGS;
			((u_int64_t *)(uintptr_t)l2)[1] =
			    (ARM64_PHYSBASE + 0x200000) | PTE_BLOCK_NORMAL_FLAGS;
			uart_puts("[arm64] pt l2=0x");
			uart_puthex(l2);
			uart_puts("\r\n");
			uart_puts("[arm64] pt l2[0]=0x");
			uart_puthex(((u_int64_t *)(uintptr_t)l2)[0]);
			uart_puts("\r\n");
			uart_puts("[arm64] pt l2[1]=0x");
			uart_puthex(((u_int64_t *)(uintptr_t)l2)[1]);
			uart_puts("\r\n");
		} else {
			uart_puts("[arm64] boot_alloc l2 failed\r\n");
		}
		arm64_ttbr1_candidate = pt;
		uart_puts("[arm64] pt l0[0]=0x");
		uart_puthex(((u_int64_t *)(uintptr_t)pt)[0]);
		uart_puts("\r\n");
		uart_puts("[arm64] pt l1[0]=0x");
		uart_puthex(((u_int64_t *)(uintptr_t)(pt + 4096))[0]);
		uart_puts("\r\n");
	} else {
		uart_puts("[arm64] boot_alloc pt failed\r\n");
	}
}

static void
arm64_ttbr1_switch(void)
{
	u_int64_t current;

	__asm __volatile("mrs %0, ttbr1_el1" : "=r" (current));
	uart_puts("[arm64] ttbr1 current=0x");
	uart_puthex(current);
	uart_puts("\r\n");
	uart_puts("[arm64] ttbr1 candidate=0x");
	uart_puthex(arm64_ttbr1_candidate);
	uart_puts("\r\n");
	if (arm64_ttbr1_candidate == 0)
		return;

	uart_puts("[arm64] ttbr1 switching...\r\n");

	__asm __volatile(
	    "dsb sy\n"
	    "msr ttbr1_el1, %0\n"
	    "isb\n"
	    "tlbi vmalle1\n"
	    "dsb sy\n"
	    "isb\n"
	    :: "r" (arm64_ttbr1_candidate) : "memory");

	uart_puts("[arm64] ttbr1 switch done, calling trampoline\r\n");

	void (*tramp)(void) =
	    (void (*)(void))((uintptr_t)&arm64_high_trampoline + ARM64_PTOTV_OFF);

	uart_puts("[arm64] trampoline addr=0x");
	uart_puthex((u_int64_t)(uintptr_t)tramp);
	uart_puts("\r\n");

	tramp();
	uart_puts("[arm64] ttbr1 switch active\r\n");
}

static volatile u_int32_t *const uart_base = (u_int32_t *)0x09000000;

static const u_int32_t modinfo_end = 0x0000;
static const u_int32_t modinfo_name = 0x0001;
static const u_int32_t modinfo_metadata = 0x8000;
static const u_int32_t modinfomd_kernend = 0x0008;

uintptr_t boot_modulep;
uintptr_t efi_systbl_phys;
char *ptvmmap;

volatile cpumask_t stopped_cpus;

/*
 * Dummy variable used by _get_mycpu() to force the compiler to
 * generate a memory reference.  On arm64, TPIDR_EL1 is used for
 * per-CPU data, but the inline assembly needs a memory clobber.
 */
int __mycpu__dummy;

static caddr_t preload_kmdp;

static void
preload_initkmdp(void)
{
	caddr_t curp, lname;
	u_int32_t *hdr;
	int next;

	preload_kmdp = NULL;
	if (preload_metadata == NULL)
		return;

	curp = preload_metadata;
	lname = NULL;
	for (;;) {
		hdr = (u_int32_t *)curp;
		if (hdr[0] == 0 && hdr[1] == 0)
			break;
		if (hdr[0] == MODINFO_NAME)
			lname = curp;
		if (hdr[0] == MODINFO_TYPE &&
		    (md_strcmp("elf kernel", curp + sizeof(u_int32_t) * 2) == 0 ||
		    md_strcmp("elf64 kernel", curp + sizeof(u_int32_t) * 2) == 0)) {
			preload_kmdp = lname;
			return;
		}
		next = sizeof(u_int32_t) * 2 + hdr[1];
		next = roundup(next, sizeof(u_long));
		curp += next;
	}
}

static uintptr_t
md_fetch_uintptr(caddr_t mdp, int info)
{
	uintptr_t *ptr;

	ptr = (uintptr_t *)preload_search_info(mdp, MODINFO_METADATA | info);
	return (ptr == NULL) ? 0 : *ptr;
}

static void *
md_fetch_ptr(caddr_t mdp, int info)
{
	return preload_search_info(mdp, MODINFO_METADATA | info);
}

static int
md_fetch_int(caddr_t mdp, int info)
{
	int *ptr;

	ptr = (int *)preload_search_info(mdp, MODINFO_METADATA | info);
	return (ptr == NULL) ? 0 : *ptr;
}

static void
uart_putc(char ch)
{
	*uart_base = (u_int32_t)(unsigned char)ch;
}

static void
uart_puts(const char *str)
{
	while (*str != '\0') {
		uart_putc(*str++);
	}
}

static void
uart_puthex(u_int64_t value)
{
	const char *hex = "0123456789abcdef";
	int shift;

	for (shift = 60; shift >= 0; shift -= 4)
		uart_putc(hex[(value >> shift) & 0xf]);
}

static uintptr_t
roundup_uintptr(uintptr_t value, uintptr_t align)
{
	return (value + align - 1) & ~(align - 1);
}

static void
dump_modulep_headers(const char *label, uintptr_t modulep)
{
	u_int32_t *hdr;
	uintptr_t next;
	int i;

	hdr = (u_int32_t *)modulep;

	uart_puts("[arm64] ");
	uart_puts(label);
	uart_puts(" header: ");
	for (i = 0; i < 4; i++) {
		uart_puthex(hdr[0]);
		uart_putc(' ');
		uart_puthex(hdr[1]);
		uart_putc(' ');
		next = sizeof(u_int32_t) * 2 + hdr[1];
		next = roundup_uintptr(next, sizeof(uintptr_t));
		hdr = (u_int32_t *)((uintptr_t)hdr + next);
	}
	uart_puts("\r\n");

}

static void
parse_modulep(uintptr_t modulep)
{
	u_int32_t *hdr;
	uintptr_t next;
	const char *name;
	uintptr_t kernend;

	dump_modulep_headers("modulep(phys)", modulep);

	hdr = (u_int32_t *)modulep;
	kernend = 0;

	hdr = (u_int32_t *)modulep;

	for (;;) {
		if (hdr[0] == modinfo_end && hdr[1] == modinfo_end)
			break;
		if (hdr[0] == modinfo_name) {
			name = (const char *)(hdr + 2);
			uart_puts("[arm64] module: ");
			uart_puts(name);
			uart_puts("\r\n");
		} else if (hdr[0] == (modinfo_metadata | modinfomd_kernend)) {
			kernend = *(uintptr_t *)(hdr + 2);
		}
		next = sizeof(u_int32_t) * 2 + hdr[1];
		next = roundup_uintptr(next, sizeof(uintptr_t));
		hdr = (u_int32_t *)((uintptr_t)hdr + next);
	}

	if (kernend != 0) {
		uart_puts("[arm64] kernend=0x");
		uart_puthex((u_int64_t)kernend);
		uart_puts("\r\n");
	}
}

void
initarm(uintptr_t modulep)
{
	boot_modulep = modulep;

	uart_puts("\033[2J\033[H");

	if (modulep == 0) {
		uart_puts("[arm64] initarm: modulep missing\r\n");
		return;
	}

	/*
	 * Keep modulep as physical address for now.
	 * Early page tables only map first 4MB at high VA, but modulep
	 * may be at ~12MB offset. TTBR0 identity maps 0x40000000-0x7fffffff
	 * so we can access physical addresses directly.
	 */
	if (modulep >= ARM64_KERNBASE)
		modulep -= ARM64_PTOTV_OFF;

	uart_puts("[arm64] initarm: modulep(phys)=0x");
	uart_puthex((u_int64_t)modulep);
	uart_puts("\r\n");

	parse_modulep(modulep);

	preload_metadata = (caddr_t)modulep;
	preload_bootstrap_relocate(0);
	preload_initkmdp();
	if (preload_kmdp == NULL) {
		uart_puts("[arm64] no kernel metadata\r\n");
		return;
	}

	boothowto = md_fetch_int(preload_kmdp, MODINFOMD_HOWTO);
	kern_envp = (char *)md_fetch_uintptr(preload_kmdp, MODINFOMD_ENVP);
	efi_systbl_phys = md_fetch_uintptr(preload_kmdp, MODINFOMD_FW_HANDLE);
	struct efi_map_header *efihdr =
	    (struct efi_map_header *)md_fetch_ptr(preload_kmdp, MODINFOMD_EFI_MAP);

	uart_puts("[arm64] boothowto=0x");
	uart_puthex((u_int64_t)boothowto);
	uart_puts("\r\n");
	uart_puts("[arm64] kern_envp=0x");
	uart_puthex((u_int64_t)kern_envp);
	uart_puts("\r\n");
	uart_puts("[arm64] efi_systbl=0x");
	uart_puthex((u_int64_t)efi_systbl_phys);
	uart_puts("\r\n");

	if (efihdr != NULL && efihdr->descriptor_size != 0) {
		u_int64_t count = efihdr->memory_size / efihdr->descriptor_size;
		uart_puts("[arm64] efi_map entries=");
		uart_puthex(count);
		uart_puts("\r\n");
		u_int64_t usable = 0;
		u_int64_t efisz = (sizeof(*efihdr) + 0xf) & ~0xf;
		u_int8_t *desc = (u_int8_t *)efihdr + efisz;
		for (u_int64_t i = 0; i < count; i++) {
			struct efi_md *md = (struct efi_md *)(void *)desc;
			if (md->type == EFI_MD_TYPE_CONVENTIONAL) {
				usable += md->num_pages;
				if (arm64_physmem_count < ARM64_MAX_PHYSMEM_RANGES) {
					struct arm64_phys_range *range =
					    &arm64_physmem[arm64_physmem_count++];
					range->start = md->phys_start;
					range->end = md->phys_start +
					    (md->num_pages * 4096);
				}
			}
			desc += efihdr->descriptor_size;
		}
		uart_puts("[arm64] efi_map usable_pages=");
		uart_puthex(usable);
		uart_puts("\r\n");
		uart_puts("[arm64] physmem ranges=");
		uart_puthex((u_int64_t)arm64_physmem_count);
		uart_puts("\r\n");
		for (int i = 0; i < arm64_physmem_count; i++) {
			uart_puts("[arm64] physmem[");
			uart_puthex((u_int64_t)i);
			uart_puts("] 0x");
			uart_puthex(arm64_physmem[i].start);
			uart_puts("-0x");
			uart_puthex(arm64_physmem[i].end);
			uart_puts("\r\n");
		}
		arm64_pmap_bootstrap(arm64_physmem, arm64_physmem_count);
		arm64_ttbr1_switch();
	} else {
		uart_puts("[arm64] no efi map\r\n");
	}
}

void
smp_sniff(void)
{
}

void
smp_invltlb(void)
{
}

int
stop_cpus(cpumask_t map)
{
	return (1);
}

int
restart_cpus(cpumask_t map)
{
	return (1);
}

void
cpu_send_ipiq(int ipi)
{
}

int
cpu_send_ipiq_passive(int ipi)
{
	return (0);
}

void
cpu_sniff(int dcpu)
{
}

/*
 * TLS sanitization stub for arm64.
 * ARM64 uses TPIDR_EL0 register for TLS, no segment descriptor
 * validation needed like x86.
 */
int
cpu_sanitize_tls(struct savetls *tls __unused)
{
	return (0);
}

/*
 * SMP related stubs
 */
cpumask_t smp_active_mask;
int naps;	/* Number of application processors (non-boot CPUs) */

/*
 * globaldata_find - find globaldata for given CPU
 *
 * For UP system, only CPU 0 exists.
 * Returns mycpu for cpu 0, NULL otherwise.
 */
struct globaldata *
globaldata_find(int cpu)
{
	if (cpu == 0)
		return (mycpu);
	return (NULL);
}

/*
 * cpu_fork - machine-dependent fork handling
 */
void
cpu_fork(struct lwp *lp1 __unused, struct lwp *lp2 __unused, int flags __unused)
{
}

/*
 * cpu_vmspace_alloc - allocate machine-dependent vmspace structures
 */
void
cpu_vmspace_alloc(struct vmspace *vm __unused)
{
}

/*
 * cpu_vmspace_free - free machine-dependent vmspace structures
 */
void
cpu_vmspace_free(struct vmspace *vm __unused)
{
}

/*
 * set_user_TLS - set user TLS
 */
void
set_user_TLS(void)
{
}

/*
 * fls - find last set bit
 *
 * Returns the position of the most significant bit set, or 0 if no bits set.
 */
int
fls(int mask)
{
	int bit;

	if (mask == 0)
		return (0);
	for (bit = 1; mask != 1; bit++)
		mask = (unsigned int)mask >> 1;
	return (bit);
}

/*
 * VM page dump variables - used for crash dumps
 */
uint64_t *vm_page_dump;
vm_offset_t vm_page_dump_size;

/*
 * Loopback interface pointer - will be set by loopback driver
 */
struct ifnet *loif;

/*
 * Ethernet stubs - these need proper implementations for networking
 */
void
ether_demux_oncpu(struct ifnet *ifp __unused, struct mbuf *m __unused)
{
}

int
ether_output_frame(struct ifnet *ifp __unused, struct mbuf *m __unused)
{
	return (0);
}

int
if_simloop(struct ifnet *ifp __unused, struct mbuf *m __unused,
    int af __unused, int hlen __unused)
{
	return (0);
}

/*
 * arp_gratuitous - send gratuitous ARP (stub)
 */
void
arp_gratuitous(struct ifnet *ifp __unused, struct ifaddr *ifa __unused)
{
}

/*
 * Root device - will be set during boot
 */
cdev_t rootdev;

/*
 * inittodr - initialize time of day register
 */
void
inittodr(time_t base __unused)
{
}

/*
 * procfs register access stubs
 */
int
procfs_read_regs(struct lwp *lp __unused, struct reg *regs __unused)
{
	return (EOPNOTSUPP);
}

int
procfs_write_regs(struct lwp *lp __unused, struct reg *regs __unused)
{
	return (EOPNOTSUPP);
}

int
procfs_read_fpregs(struct lwp *lp __unused, struct fpreg *fpregs __unused)
{
	return (EOPNOTSUPP);
}

int
procfs_write_fpregs(struct lwp *lp __unused, struct fpreg *fpregs __unused)
{
	return (EOPNOTSUPP);
}

int
procfs_read_dbregs(struct lwp *lp __unused, struct dbreg *dbregs __unused)
{
	return (EOPNOTSUPP);
}

int
procfs_write_dbregs(struct lwp *lp __unused, struct dbreg *dbregs __unused)
{
	return (EOPNOTSUPP);
}

/*
 * Vkernel stubs - these are called from generic vm_vmspace.c but
 * vkernel is not implemented on arm64. Return errors/do nothing.
 */
int
cpu_sanitize_frame(struct trapframe *frame __unused)
{
	return (ENOSYS);
}

void
cpu_vkernel_trap(struct trapframe *frame __unused, int error __unused)
{
}

void
set_vkernel_fp(struct trapframe *frame __unused)
{
}

/*
 * splz - process software interrupts when lowering SPL
 *
 * On arm64 this is a stub for now. Full implementation will need to
 * process pending software interrupts.
 */
void
splz(void)
{
}

/*
 * setsofttq - set software taskqueue interrupt pending
 */
void
setsofttq(void)
{
}

/*
 * resettodr - reset time of day register to system time
 */
void
resettodr(void)
{
}

/*
 * MachIntrABI - machine interrupt ABI
 *
 * Stub implementation with NULL function pointers. The kernel will
 * need proper interrupt controller support (GIC) before this works.
 */
static void machintr_stub_intr_disable(int intr __unused) {}
static void machintr_stub_intr_enable(int intr __unused) {}
static void machintr_stub_intr_setup(int intr __unused, int flags __unused) {}
static void machintr_stub_intr_teardown(int intr __unused) {}
static void machintr_stub_legacy_intr_config(int intr __unused,
    enum intr_trigger trig __unused, enum intr_polarity pola __unused) {}
static int machintr_stub_legacy_intr_cpuid(int intr __unused) { return 0; }
static int machintr_stub_legacy_intr_find(int intr __unused,
    enum intr_trigger trig __unused, enum intr_polarity pola __unused)
    { return -1; }
static int machintr_stub_legacy_intr_find_bygsi(int gsi __unused,
    enum intr_trigger trig __unused, enum intr_polarity pola __unused)
    { return -1; }
static int machintr_stub_msi_alloc(int intrs[] __unused, int count __unused,
    int cpuid __unused) { return EOPNOTSUPP; }
static void machintr_stub_msi_release(const int intrs[] __unused,
    int count __unused, int cpuid __unused) {}
static void machintr_stub_msi_map(int intr __unused, uint64_t *addr __unused,
    uint32_t *data __unused, int cpuid __unused) {}
static int machintr_stub_msix_alloc(int *intr __unused, int cpuid __unused)
    { return EOPNOTSUPP; }
static void machintr_stub_msix_release(int intr __unused, int cpuid __unused) {}
static void machintr_stub_finalize(void) {}
static void machintr_stub_cleanup(void) {}
static void machintr_stub_setdefault(void) {}
static void machintr_stub_stabilize(void) {}
static void machintr_stub_initmap(void) {}
static void machintr_stub_rman_setup(struct rman *rm __unused) {}

struct machintr_abi MachIntrABI = {
	.type = MACHINTR_GENERIC,
	.intr_disable = machintr_stub_intr_disable,
	.intr_enable = machintr_stub_intr_enable,
	.intr_setup = machintr_stub_intr_setup,
	.intr_teardown = machintr_stub_intr_teardown,
	.legacy_intr_config = machintr_stub_legacy_intr_config,
	.legacy_intr_cpuid = machintr_stub_legacy_intr_cpuid,
	.legacy_intr_find = machintr_stub_legacy_intr_find,
	.legacy_intr_find_bygsi = machintr_stub_legacy_intr_find_bygsi,
	.msi_alloc = machintr_stub_msi_alloc,
	.msi_release = machintr_stub_msi_release,
	.msi_map = machintr_stub_msi_map,
	.msix_alloc = machintr_stub_msix_alloc,
	.msix_release = machintr_stub_msix_release,
	.finalize = machintr_stub_finalize,
	.cleanup = machintr_stub_cleanup,
	.setdefault = machintr_stub_setdefault,
	.stabilize = machintr_stub_stabilize,
	.initmap = machintr_stub_initmap,
	.rman_setup = machintr_stub_rman_setup,
};

/*
 * Kernel message buffer pointer
 */
struct msgbuf *msgbufp;

/*
 * ffs - find first set bit (LSB)
 *
 * Returns the position of the least significant bit set (1-indexed),
 * or 0 if no bits are set.
 */
int
ffs(int mask)
{
	int bit;

	if (mask == 0)
		return (0);
	for (bit = 1; (mask & 1) == 0; bit++)
		mask = (unsigned int)mask >> 1;
	return (bit);
}

/*
 * ptrace_set_pc - set program counter for ptrace
 */
int
ptrace_set_pc(struct lwp *lp __unused, unsigned long addr __unused)
{
	return (EOPNOTSUPP);
}

/*
 * ptrace_single_step - enable single-stepping for ptrace
 */
int
ptrace_single_step(struct lwp *lp __unused)
{
	return (EOPNOTSUPP);
}

/*
 * mbrinit - initialize MBR partition table (stub)
 *
 * This is called from disk subsystem but arm64 systems typically
 * use GPT. Return EOPNOTSUPP for now.
 */
int
mbrinit(cdev_t dev __unused, struct disk_info *info __unused,
    struct diskslices **sspp __unused)
{
	return (EOPNOTSUPP);
}

/*
 * flsl - find last set bit (long version)
 *
 * Returns the position of the most significant bit set (1-indexed),
 * or 0 if no bits are set.
 */
int
flsl(long mask)
{
	int bit;

	if (mask == 0)
		return (0);
	for (bit = 1; mask != 1; bit++)
		mask = (unsigned long)mask >> 1;
	return (bit);
}

/*
 * TSC (timestamp counter) stubs
 * ARM64 doesn't have x86 TSC, uses different counters.
 */
int tsc_present;
tsc_uclock_t tsc_frequency;
tsc_uclock_t tsc_oneus_approx = 1;

/*
 * DELAY - spin-wait for specified number of microseconds
 */
void
DELAY(int usec)
{
	volatile int i;

	/* Very rough delay - needs proper timer implementation */
	while (usec-- > 0) {
		for (i = 0; i < 100; i++)
			;
	}
}

/*
 * cpu_reset - reset the CPU
 */
void
cpu_reset(void)
{
	/* ARM64: would use PSCI or direct register write */
	for (;;)
		;
}

/*
 * cpu_halt - halt the CPU
 */
void
cpu_halt(void)
{
	/* ARM64: WFI instruction in loop */
	for (;;)
		__asm __volatile("wfi");
}

/*
 * Crash dump support stubs
 */
cdev_t dumpdev;

void
md_dumpsys(struct dumperinfo *di __unused)
{
}

void
savectx(struct pcb *pcb __unused)
{
}

/*
 * I/O privilege level stubs (x86-specific concept)
 * On ARM64 these are no-ops.
 */
int
cpu_set_iopl(void)
{
	return (0);
}

int
cpu_clr_iopl(void)
{
	return (0);
}

/*
 * kvm_access_check - check kernel memory access (stub)
 */
int
kvm_access_check(vm_offset_t saddr __unused, vm_offset_t eaddr __unused,
    int prot __unused)
{
	return (0);	/* Allow access for now */
}

/*
 * User memory access stubs
 */
uint32_t
fuwordadd32(volatile uint32_t *base __unused, uint32_t v __unused)
{
	return ((uint32_t)-1);	/* Fail */
}

vm_paddr_t
uservtophys(vm_offset_t va __unused)
{
	return ((vm_paddr_t)-1);	/* Invalid */
}

/*
 * Thread/CPU support stubs
 */
void
cpu_thread_exit(void)
{
	/* Function is __dead2 (noreturn) - loop forever */
	for (;;)
		;
}

void
cpu_set_thread_handler(struct thread *td __unused,
    void (*retfunc)(void) __unused, void *func __unused, void *arg __unused)
{
}

void
cpu_smp_stopped(void)
{
}

/*
 * Register access functions for ptrace/core dumps
 */
int
fill_regs(struct lwp *lp __unused, struct reg *regs)
{
	bzero(regs, sizeof(*regs));
	return (0);
}

int
set_regs(struct lwp *lp __unused, struct reg *regs __unused)
{
	return (0);
}

int
fill_fpregs(struct lwp *lp __unused, struct fpreg *fpregs)
{
	bzero(fpregs, sizeof(*fpregs));
	return (0);
}

int
set_fpregs(struct lwp *lp __unused, struct fpreg *fpregs __unused)
{
	return (0);
}

/*
 * Context switch functions - stubs for now
 * These need assembly implementations for proper context switching.
 */
struct thread *
cpu_lwkt_switch(struct thread *ntd __unused)
{
	panic("cpu_lwkt_switch: not implemented");
	return (NULL);	/* not reached */
}

struct thread *
cpu_heavy_switch(struct thread *ntd __unused)
{
	panic("cpu_heavy_switch: not implemented");
	return (NULL);	/* not reached */
}

void
cpu_set_fork_handler(struct lwp *lp __unused,
    void (*func)(void *, struct trapframe *) __unused, void *arg __unused)
{
}

/*
 * Signal handling
 */
void
sendsig(sig_t catcher __unused, int sig __unused, sigset_t *mask __unused,
    u_long code __unused)
{
	panic("sendsig: not implemented");
}

/*
 * sigcode - signal trampoline code
 * This is machine code that gets copied to user stack for signal return.
 * On ARM64, this will be ARM64 instructions for sigreturn syscall.
 */
char sigcode[] = {
	0x00, 0x00, 0x00, 0x00	/* placeholder - needs real signal trampoline */
};
int szsigcode = sizeof(sigcode);

/*
 * Syscall stubs
 */
int
sys_xsyscall(struct sysmsg *sysmsg __unused,
    const struct nosys_args *uap __unused)
{
	return (ENOSYS);
}

int
sys_sysarch(struct sysmsg *sysmsg __unused,
    const struct sysarch_args *uap __unused)
{
	return (EOPNOTSUPP);
}

int
sys_sigreturn(struct sysmsg *sysmsg __unused,
    const struct sigreturn_args *uap __unused)
{
	return (EOPNOTSUPP);
}

int
sys_set_tls_area(struct sysmsg *sysmsg __unused,
    const struct set_tls_area_args *uap __unused)
{
	return (EOPNOTSUPP);
}

int
sys_get_tls_area(struct sysmsg *sysmsg __unused,
    const struct get_tls_area_args *uap __unused)
{
	return (EOPNOTSUPP);
}

/*
 * ELF relocation stubs
 * These are needed for kernel module loading.
 */
int
elf_reloc(linker_file_t lf __unused, Elf_Addr relocbase __unused,
    const void *data __unused, int type __unused,
    elf_lookup_fn lookup __unused)
{
	return (EOPNOTSUPP);
}

int
elf_reloc_local(linker_file_t lf __unused, Elf_Addr relocbase __unused,
    const void *data __unused, int type __unused,
    elf_lookup_fn lookup __unused)
{
	return (EOPNOTSUPP);
}

/*
 * Interrupt/timer support stubs
 */
int
cpu_interrupt_running(struct thread *td __unused)
{
	return (0);
}

void
setdelayed(void)
{
}

/*
 * exec_setregs - set registers for new process after exec
 */
void
exec_setregs(u_long entry __unused, u_long stack __unused,
    u_long ps_strings __unused)
{
}

/*
 * LWP support functions
 */
void
cpu_lwp_exit(void)
{
	panic("cpu_lwp_exit: not implemented");
}

int
cpu_prepare_lwp(struct lwp *lp __unused, struct lwp_params *params __unused)
{
	return (0);
}

/*
 * 128-bit division support for compiler
 * GCC generates calls to this for 128-bit integer division.
 * This is a minimal stub - real implementation would need full division.
 */
typedef unsigned __int128 uint128_t;

uint128_t __udivti3(uint128_t a, uint128_t b);

uint128_t
__udivti3(uint128_t a, uint128_t b)
{
	/* Minimal stub - will fail for large values */
	if (b == 0)
		panic("__udivti3: division by zero");
	if (b == 1)
		return a;
	/* Very slow fallback for small divisors */
	uint128_t result = 0;
	while (a >= b) {
		a -= b;
		result++;
	}
	return result;
}
