/*-
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 1993 The Regents of the University of California.
 * Copyright (c) 2008 The DragonFly Project.
 * All rights reserved.
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
 * $FreeBSD: src/sys/amd64/include/cpufunc.h,v 1.139 2004/01/28 23:53:04 peter Exp $
 */

/*
 * Functions to provide access to special i386 instructions.
 * This in included in sys/systm.h, and that file should be
 * used in preference to this.
 */

#ifndef _CPU_CPUFUNC_H_
#define	_CPU_CPUFUNC_H_

#include <sys/cdefs.h>
#include <sys/thread.h>
#include <machine/psl.h>
#include <machine/smp.h>

struct thread;
struct region_descriptor;

__BEGIN_DECLS
#define readb(va)	(*(volatile u_int8_t *) (va))
#define readw(va)	(*(volatile u_int16_t *) (va))
#define readl(va)	(*(volatile u_int32_t *) (va))
#define readq(va)	(*(volatile u_int64_t *) (va))

#define writeb(va, d)	(*(volatile u_int8_t *) (va) = (d))
#define writew(va, d)	(*(volatile u_int16_t *) (va) = (d))
#define writel(va, d)	(*(volatile u_int32_t *) (va) = (d))
#define writeq(va, d)	(*(volatile u_int64_t *) (va) = (d))

#ifdef	__GNUC__

#include <machine/lock.h>		/* XXX */

static __inline void
breakpoint(void)
{
	__asm __volatile("int $3");
}

static __inline void
cpu_pause(void)
{
	__asm __volatile("pause":::"memory");
}

static __inline u_int
bsfl(u_int mask)
{
	u_int	result;

	__asm __volatile("bsfl %1,%0" : "=r" (result) : "rm" (mask));
	return (result);
}

static __inline u_long
bsfq(u_long mask)
{
	u_long	result;

	__asm __volatile("bsfq %1,%0" : "=r" (result) : "rm" (mask));
	return (result);
}

static __inline u_long
bsflong(u_long mask)
{
	u_long	result;

	__asm __volatile("bsfq %1,%0" : "=r" (result) : "rm" (mask));
	return (result);
}

static __inline u_int
bsrl(u_int mask)
{
	u_int	result;

	__asm __volatile("bsrl %1,%0" : "=r" (result) : "rm" (mask));
	return (result);
}

static __inline u_long
bsrq(u_long mask)
{
	u_long	result;

	__asm __volatile("bsrq %1,%0" : "=r" (result) : "rm" (mask));
	return (result);
}

static __inline void
clflush(u_long addr)
{
	__asm __volatile("clflush %0" : : "m" (*(char *) addr));
}

static __inline void
do_cpuid(u_int ax, u_int *p)
{
	__asm __volatile("cpuid"
			 : "=a" (p[0]), "=b" (p[1]), "=c" (p[2]), "=d" (p[3])
			 :  "0" (ax));
}

static __inline void
cpuid_count(u_int ax, u_int cx, u_int *p)
{
	__asm __volatile("cpuid"
			 : "=a" (p[0]), "=b" (p[1]), "=c" (p[2]), "=d" (p[3])
			 :  "0" (ax), "c" (cx));
}

#ifndef _CPU_DISABLE_INTR_DEFINED

static __inline void
cpu_disable_intr(void)
{
	__asm __volatile("cli" : : : "memory");
}

#endif

#ifndef _CPU_ENABLE_INTR_DEFINED

static __inline void
cpu_enable_intr(void)
{
	__asm __volatile("sti");
}

#endif

/*
 * Cpu and compiler memory ordering fence.  mfence ensures strong read and
 * write ordering.
 *
 * A serializing or fence instruction is required here.  A locked bus
 * cycle on data for which we already own cache mastership is the most
 * portable.
 */
static __inline void
cpu_mfence(void)
{
	__asm __volatile("mfence" : : : "memory");
}

/*
 * cpu_lfence() ensures strong read ordering for reads issued prior
 * to the instruction verses reads issued afterwords.
 *
 * A serializing or fence instruction is required here.  A locked bus
 * cycle on data for which we already own cache mastership is the most
 * portable.
 */
static __inline void
cpu_lfence(void)
{
	__asm __volatile("lfence" : : : "memory");
}

/*
 * cpu_sfence() ensures strong write ordering for writes issued prior
 * to the instruction verses writes issued afterwords.  Writes are
 * ordered on intel cpus so we do not actually have to do anything.
 */
static __inline void
cpu_sfence(void)
{
	/*
	 * NOTE:
	 * Don't use 'sfence' here, as it will create a lot of
	 * unnecessary stalls.
	 */
	__asm __volatile("" : : : "memory");
}

/*
 * cpu_ccfence() prevents the compiler from reordering instructions, in
 * particular stores, relative to the current cpu.  Use cpu_sfence() if
 * you need to guarentee ordering by both the compiler and by the cpu.
 *
 * This also prevents the compiler from caching memory loads into local
 * variables across the routine.
 */
static __inline void
cpu_ccfence(void)
{
	__asm __volatile("" : : : "memory");
}

/*
 * This is a horrible, horrible hack that might have to be put at the
 * end of certain procedures (on a case by case basis), just before it
 * returns to avoid what we believe to be an unreported AMD cpu bug.
 * Found to occur on both a Phenom II X4 820 (two of them), as well
 * as a 48-core built around an Opteron 6168 (Id = 0x100f91  Stepping = 1).
 * The problem does not appear to occur w/Intel cpus.
 *
 * The bug is likely related to either a write combining issue or the
 * Return Address Stack (RAS) hardware cache.
 *
 * In particular, we had to do this for GCC's fill_sons_in_loop() routine
 * which due to its deep recursion and stack flow appears to be able to
 * tickle the amd cpu bug (w/ gcc-4.4.7).  Adding a single 'nop' to the
 * end of the routine just before it returns works around the bug.
 *
 * The bug appears to be extremely sensitive to %rip and %rsp values, to
 * the point where even just inserting an instruction in an unrelated
 * procedure (shifting the entire code base being run) effects the outcome.
 * DragonFly is probably able to more readily reproduce the bug due to
 * the stackgap randomization code.  We would expect OpenBSD (where we got
 * the stackgap randomization code from) to also be able to reproduce the
 * issue.  To date we have only reproduced the issue in DragonFly.
 */
#define __AMDCPUBUG_DFLY01_AVAILABLE__

static __inline void
cpu_amdcpubug_dfly01(void)
{
	__asm __volatile("nop" : : : "memory");
}

#ifdef _KERNEL

#define	HAVE_INLINE_FFS

static __inline int
ffs(int mask)
{
#if 0
	/*
	 * Note that gcc-2's builtin ffs would be used if we didn't declare
	 * this inline or turn off the builtin.  The builtin is faster but
	 * broken in gcc-2.4.5 and slower but working in gcc-2.5 and later
	 * versions.
	 */
	return (mask == 0 ? mask : (int)bsfl((u_int)mask) + 1);
#else
	/* Actually, the above is way out of date.  The builtins use cmov etc */
	return (__builtin_ffs(mask));
#endif
}

#define	HAVE_INLINE_FFSL

static __inline int
ffsl(long mask)
{
	return (mask == 0 ? mask : (int)bsfq((u_long)mask) + 1);
}

#define	HAVE_INLINE_FLS

static __inline int
fls(int mask)
{
	return (mask == 0 ? mask : (int)bsrl((u_int)mask) + 1);
}

#define	HAVE_INLINE_FLSL

static __inline int
flsl(long mask)
{
	return (mask == 0 ? mask : (int)bsrq((u_long)mask) + 1);
}

#endif /* _KERNEL */

static __inline void
halt(void)
{
	__asm __volatile("hlt");
}

/*
 * The following complications are to get around gcc not having a
 * constraint letter for the range 0..255.  We still put "d" in the
 * constraint because "i" isn't a valid constraint when the port
 * isn't constant.  This only matters for -O0 because otherwise
 * the non-working version gets optimized away.
 * 
 * Use an expression-statement instead of a conditional expression
 * because gcc-2.6.0 would promote the operands of the conditional
 * and produce poor code for "if ((inb(var) & const1) == const2)".
 *
 * The unnecessary test `(port) < 0x10000' is to generate a warning if
 * the `port' has type u_short or smaller.  Such types are pessimal.
 * This actually only works for signed types.  The range check is
 * careful to avoid generating warnings.
 */
#define	inb(port) __extension__ ({					\
	u_char	_data;							\
	if (__builtin_constant_p(port) && ((port) & 0xffff) < 0x100	\
	    && (port) < 0x10000)					\
		_data = inbc(port);					\
	else								\
		_data = inbv(port);					\
	_data; })

#define	outb(port, data) (						\
	__builtin_constant_p(port) && ((port) & 0xffff) < 0x100		\
	&& (port) < 0x10000						\
	? outbc(port, data) : outbv(port, data))

static __inline u_char
inbc(u_int port)
{
	u_char	data;

	__asm __volatile("inb %1,%0" : "=a" (data) : "id" ((u_short)(port)));
	return (data);
}

static __inline void
outbc(u_int port, u_char data)
{
	__asm __volatile("outb %0,%1" : : "a" (data), "id" ((u_short)(port)));
}

static __inline u_char
inbv(u_int port)
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

static __inline u_int
inl(u_int port)
{
	u_int	data;

	__asm __volatile("inl %%dx,%0" : "=a" (data) : "d" (port));
	return (data);
}

static __inline void
insb(u_int port, void *addr, size_t cnt)
{
	__asm __volatile("cld; rep; insb"
			 : "+D" (addr), "+c" (cnt)
			 : "d" (port)
			 : "memory");
}

static __inline void
insw(u_int port, void *addr, size_t cnt)
{
	__asm __volatile("cld; rep; insw"
			 : "+D" (addr), "+c" (cnt)
			 : "d" (port)
			 : "memory");
}

static __inline void
insl(u_int port, void *addr, size_t cnt)
{
	__asm __volatile("cld; rep; insl"
			 : "+D" (addr), "+c" (cnt)
			 : "d" (port)
			 : "memory");
}

static __inline void
invd(void)
{
	__asm __volatile("invd");
}

#if defined(_KERNEL)

/*
 * If we are not a true-SMP box then smp_invltlb() is a NOP.  Note that this
 * will cause the invl*() functions to be equivalent to the cpu_invl*()
 * functions.
 */
void smp_invltlb(void);
void smp_invltlb_intr(void);

#ifndef _CPU_INVLPG_DEFINED

/*
 * Invalidate a particular VA on this cpu only
 *
 * TLB flush for an individual page (even if it has PG_G).
 * Only works on 486+ CPUs (i386 does not have PG_G).
 */
static __inline void
cpu_invlpg(void *addr)
{
	__asm __volatile("invlpg %0" : : "m" (*(char *)addr) : "memory");
}

#endif

static __inline void
cpu_nop(void)
{
	__asm __volatile("rep; nop");
}

#endif	/* _KERNEL */

static __inline u_short
inw(u_int port)
{
	u_short	data;

	__asm __volatile("inw %%dx,%0" : "=a" (data) : "d" (port));
	return (data);
}

static __inline u_int
loadandclear(volatile u_int *addr)
{
	u_int   result;

	__asm __volatile("xorl %0,%0; xchgl %1,%0"
			: "=&r" (result) : "m" (*addr));
	return (result);
}

static __inline void
outbv(u_int port, u_char data)
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

static __inline void
outl(u_int port, u_int data)
{
	/*
	 * outl() and outw() aren't used much so we haven't looked at
	 * possible micro-optimizations such as the unnecessary
	 * assignment for them.
	 */
	__asm __volatile("outl %0,%%dx" : : "a" (data), "d" (port));
}

static __inline void
outsb(u_int port, const void *addr, size_t cnt)
{
	__asm __volatile("cld; rep; outsb"
			 : "+S" (addr), "+c" (cnt)
			 : "d" (port));
}

static __inline void
outsw(u_int port, const void *addr, size_t cnt)
{
	__asm __volatile("cld; rep; outsw"
			 : "+S" (addr), "+c" (cnt)
			 : "d" (port));
}

static __inline void
outsl(u_int port, const void *addr, size_t cnt)
{
	__asm __volatile("cld; rep; outsl"
			 : "+S" (addr), "+c" (cnt)
			 : "d" (port));
}

static __inline void
outw(u_int port, u_short data)
{
	__asm __volatile("outw %0,%%dx" : : "a" (data), "d" (port));
}

static __inline void
ia32_pause(void)
{
	__asm __volatile("pause");
}

static __inline u_long
read_rflags(void)
{
	u_long	rf;

	__asm __volatile("pushfq; popq %0" : "=r" (rf));
	return (rf);
}

static __inline u_int64_t
rdmsr(u_int msr)
{
	u_int32_t low, high;

	__asm __volatile("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
	return (low | ((u_int64_t)high << 32));
}

static __inline u_int64_t
rdpmc(u_int pmc)
{
	u_int32_t low, high;

	__asm __volatile("rdpmc" : "=a" (low), "=d" (high) : "c" (pmc));
	return (low | ((u_int64_t)high << 32));
}

#define _RDTSC_SUPPORTED_

static __inline u_int64_t
rdtsc(void)
{
	u_int32_t low, high;

	__asm __volatile("rdtsc" : "=a" (low), "=d" (high));
	return (low | ((u_int64_t)high << 32));
}

static __inline void
wbinvd(void)
{
	__asm __volatile("wbinvd");
}

#if defined(_KERNEL)
void cpu_wbinvd_on_all_cpus_callback(void *arg);

static __inline void
cpu_wbinvd_on_all_cpus(void)
{
	lwkt_cpusync_simple(smp_active_mask, cpu_wbinvd_on_all_cpus_callback, NULL);
}
#endif

static __inline void
write_rflags(u_long rf)
{
	__asm __volatile("pushq %0;  popfq" : : "r" (rf));
}

static __inline void
wrmsr(u_int msr, u_int64_t newval)
{
	u_int32_t low, high;

	low = newval;
	high = newval >> 32;
	__asm __volatile("wrmsr" : : "a" (low), "d" (high), "c" (msr));
}

static __inline void
xsetbv(u_int ecx, u_int eax, u_int edx)
{
	__asm __volatile(".byte 0x0f,0x01,0xd1"
	    :
	    : "a" (eax), "c" (ecx), "d" (edx));
}

static __inline void
load_cr0(u_long data)
{

	__asm __volatile("movq %0,%%cr0" : : "r" (data));
}

static __inline u_long
rcr0(void)
{
	u_long	data;

	__asm __volatile("movq %%cr0,%0" : "=r" (data));
	return (data);
}

static __inline u_long
rcr2(void)
{
	u_long	data;

	__asm __volatile("movq %%cr2,%0" : "=r" (data));
	return (data);
}

static __inline void
load_cr3(u_long data)
{

	__asm __volatile("movq %0,%%cr3" : : "r" (data) : "memory");
}

static __inline u_long
rcr3(void)
{
	u_long	data;

	__asm __volatile("movq %%cr3,%0" : "=r" (data));
	return (data);
}

static __inline void
load_cr4(u_long data)
{
	__asm __volatile("movq %0,%%cr4" : : "r" (data));
}

static __inline u_long
rcr4(void)
{
	u_long	data;

	__asm __volatile("movq %%cr4,%0" : "=r" (data));
	return (data);
}

#ifndef _CPU_INVLTLB_DEFINED

/*
 * Invalidate the TLB on this cpu only
 */
static __inline void
cpu_invltlb(void)
{
	load_cr3(rcr3());
#if defined(SWTCH_OPTIM_STATS)
	++tlb_flush_count;
#endif
}

#endif

static __inline u_short
rfs(void)
{
	u_short sel;
	__asm __volatile("movw %%fs,%0" : "=rm" (sel));
	return (sel);
}

static __inline u_short
rgs(void)
{
	u_short sel;
	__asm __volatile("movw %%gs,%0" : "=rm" (sel));
	return (sel);
}

static __inline void
load_ds(u_short sel)
{
	__asm __volatile("movw %0,%%ds" : : "rm" (sel));
}

static __inline void
load_es(u_short sel)
{
	__asm __volatile("movw %0,%%es" : : "rm" (sel));
}

#ifdef _KERNEL
/* This is defined in <machine/specialreg.h> but is too painful to get to */
#ifndef	MSR_FSBASE
#define	MSR_FSBASE	0xc0000100
#endif
static __inline void
load_fs(u_short sel)
{
	/* Preserve the fsbase value across the selector load */
	__asm __volatile("rdmsr; movw %0,%%fs; wrmsr"
            : : "rm" (sel), "c" (MSR_FSBASE) : "eax", "edx");
}

#ifndef	MSR_GSBASE
#define	MSR_GSBASE	0xc0000101
#endif
static __inline void
load_gs(u_short sel)
{
	/*
	 * Preserve the gsbase value across the selector load.
	 * Note that we have to disable interrupts because the gsbase
	 * being trashed happens to be the kernel gsbase at the time.
	 */
	__asm __volatile("pushfq; cli; rdmsr; movw %0,%%gs; wrmsr; popfq"
            : : "rm" (sel), "c" (MSR_GSBASE) : "eax", "edx");
}
#else
/* Usable by userland */
static __inline void
load_fs(u_short sel)
{
	__asm __volatile("movw %0,%%fs" : : "rm" (sel));
}

static __inline void
load_gs(u_short sel)
{
	__asm __volatile("movw %0,%%gs" : : "rm" (sel));
}
#endif

/* void lidt(struct region_descriptor *addr); */
static __inline void
lidt(struct region_descriptor *addr)
{
	__asm __volatile("lidt (%0)" : : "r" (addr));
}

/* void lldt(u_short sel); */
static __inline void
lldt(u_short sel)
{
	__asm __volatile("lldt %0" : : "r" (sel));
}

/* void ltr(u_short sel); */
static __inline void
ltr(u_short sel)
{
	__asm __volatile("ltr %0" : : "r" (sel));
}

static __inline u_int64_t
rdr0(void)
{
	u_int64_t data;
	__asm __volatile("movq %%dr0,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr0(u_int64_t dr0)
{
	__asm __volatile("movq %0,%%dr0" : : "r" (dr0));
}

static __inline u_int64_t
rdr1(void)
{
	u_int64_t data;
	__asm __volatile("movq %%dr1,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr1(u_int64_t dr1)
{
	__asm __volatile("movq %0,%%dr1" : : "r" (dr1));
}

static __inline u_int64_t
rdr2(void)
{
	u_int64_t data;
	__asm __volatile("movq %%dr2,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr2(u_int64_t dr2)
{
	__asm __volatile("movq %0,%%dr2" : : "r" (dr2));
}

static __inline u_int64_t
rdr3(void)
{
	u_int64_t data;
	__asm __volatile("movq %%dr3,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr3(u_int64_t dr3)
{
	__asm __volatile("movq %0,%%dr3" : : "r" (dr3));
}

static __inline u_int64_t
rdr4(void)
{
	u_int64_t data;
	__asm __volatile("movq %%dr4,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr4(u_int64_t dr4)
{
	__asm __volatile("movq %0,%%dr4" : : "r" (dr4));
}

static __inline u_int64_t
rdr5(void)
{
	u_int64_t data;
	__asm __volatile("movq %%dr5,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr5(u_int64_t dr5)
{
	__asm __volatile("movq %0,%%dr5" : : "r" (dr5));
}

static __inline u_int64_t
rdr6(void)
{
	u_int64_t data;
	__asm __volatile("movq %%dr6,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr6(u_int64_t dr6)
{
	__asm __volatile("movq %0,%%dr6" : : "r" (dr6));
}

static __inline u_int64_t
rdr7(void)
{
	u_int64_t data;
	__asm __volatile("movq %%dr7,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr7(u_int64_t dr7)
{
	__asm __volatile("movq %0,%%dr7" : : "r" (dr7));
}

static __inline register_t
intr_disable(void)
{
	register_t rflags;

	rflags = read_rflags();
	cpu_disable_intr();
	return (rflags);
}

static __inline void
intr_restore(register_t rflags)
{
	write_rflags(rflags);
}

#else /* !__GNUC__ */

int	breakpoint(void);
void	cpu_pause(void);
u_int	bsfl(u_int mask);
u_int	bsrl(u_int mask);
void	cpu_disable_intr(void);
void	cpu_enable_intr(void);
void	cpu_invlpg(u_long addr);
void	cpu_invlpg_range(u_long start, u_long end);
void	do_cpuid(u_int ax, u_int *p);
void	halt(void);
u_char	inb(u_int port);
u_int	inl(u_int port);
void	insb(u_int port, void *addr, size_t cnt);
void	insl(u_int port, void *addr, size_t cnt);
void	insw(u_int port, void *addr, size_t cnt);
void	invd(void);
void	invlpg_range(u_int start, u_int end);
void	cpu_invltlb(void);
u_short	inw(u_int port);
void	load_cr0(u_int cr0);
void	load_cr3(u_int cr3);
void	load_cr4(u_int cr4);
void	load_fs(u_int sel);
void	load_gs(u_int sel);
struct region_descriptor;
void	lidt(struct region_descriptor *addr);
void	lldt(u_short sel);
void	ltr(u_short sel);
void	outb(u_int port, u_char data);
void	outl(u_int port, u_int data);
void	outsb(u_int port, void *addr, size_t cnt);
void	outsl(u_int port, void *addr, size_t cnt);
void	outsw(u_int port, void *addr, size_t cnt);
void	outw(u_int port, u_short data);
void	ia32_pause(void);
u_int	rcr0(void);
u_int	rcr2(void);
u_int	rcr3(void);
u_int	rcr4(void);
u_short	rfs(void);
u_short	rgs(void);
u_int64_t rdmsr(u_int msr);
u_int64_t rdpmc(u_int pmc);
u_int64_t rdtsc(void);
u_int	read_rflags(void);
void	wbinvd(void);
void	write_rflags(u_int rf);
void	wrmsr(u_int msr, u_int64_t newval);
u_int64_t	rdr0(void);
void	load_dr0(u_int64_t dr0);
u_int64_t	rdr1(void);
void	load_dr1(u_int64_t dr1);
u_int64_t	rdr2(void);
void	load_dr2(u_int64_t dr2);
u_int64_t	rdr3(void);
void	load_dr3(u_int64_t dr3);
u_int64_t	rdr4(void);
void	load_dr4(u_int64_t dr4);
u_int64_t	rdr5(void);
void	load_dr5(u_int64_t dr5);
u_int64_t	rdr6(void);
void	load_dr6(u_int64_t dr6);
u_int64_t	rdr7(void);
void	load_dr7(u_int64_t dr7);
register_t	intr_disable(void);
void	intr_restore(register_t rf);

#endif	/* __GNUC__ */

int	rdmsr_safe(u_int msr, uint64_t *val);
void	reset_dbregs(void);

__END_DECLS

#endif /* !_CPU_CPUFUNC_H_ */
