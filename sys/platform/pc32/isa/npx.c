/*-
 * Copyright (c) 1990 William Jolitz.
 * Copyright (c) 1991 The Regents of the University of California.
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
 *	from: @(#)npx.c	7.2 (Berkeley) 5/12/91
 * $FreeBSD: src/sys/i386/isa/npx.c,v 1.80.2.3 2001/10/20 19:04:38 tegge Exp $
 * $DragonFly: src/sys/platform/pc32/isa/npx.c,v 1.49 2008/08/02 01:14:43 dillon Exp $
 */

#include "opt_cpu.h"
#include "opt_debug_npx.h"
#include "opt_math_emulate.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/rman.h>
#ifdef NPX_DEBUG
#include <sys/syslog.h>
#endif
#include <sys/signalvar.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>

#ifndef SMP
#include <machine/asmacros.h>
#endif
#include <machine/cputypes.h>
#include <machine/frame.h>
#include <machine/ipl.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#ifndef SMP
#include <machine/clock.h>
#endif
#include <machine/specialreg.h>
#include <machine/segments.h>
#include <machine/globaldata.h>

#ifndef SMP
#include <machine_base/icu/icu.h>
#include <machine/intr_machdep.h>
#include <bus/isa/isa.h>
#endif

/*
 * 387 and 287 Numeric Coprocessor Extension (NPX) Driver.
 */

/* Configuration flags. */
#define	NPX_DISABLE_I586_OPTIMIZED_BCOPY	(1 << 0)
#define	NPX_DISABLE_I586_OPTIMIZED_BZERO	(1 << 1)
#define	NPX_DISABLE_I586_OPTIMIZED_COPYIO	(1 << 2)
#define	NPX_PREFER_EMULATOR			(1 << 3)

#ifdef	__GNUC__

#define	fldcw(addr)		__asm("fldcw %0" : : "m" (*(addr)))
#define	fnclex()		__asm("fnclex")
#define	fninit()		__asm("fninit")
#define	fnop()			__asm("fnop")
#define	fnsave(addr)		__asm __volatile("fnsave %0" : "=m" (*(addr)))
#define	fnstcw(addr)		__asm __volatile("fnstcw %0" : "=m" (*(addr)))
#define	fnstsw(addr)		__asm __volatile("fnstsw %0" : "=m" (*(addr)))
#define	fp_divide_by_0()	__asm("fldz; fld1; fdiv %st,%st(1); fnop")
#define	frstor(addr)		__asm("frstor %0" : : "m" (*(addr)))
#ifndef CPU_DISABLE_SSE
#define	fxrstor(addr)		__asm("fxrstor %0" : : "m" (*(addr)))
#define	fxsave(addr)		__asm __volatile("fxsave %0" : "=m" (*(addr)))
#endif
#define	start_emulating()	__asm("smsw %%ax; orb %0,%%al; lmsw %%ax" \
				      : : "n" (CR0_TS) : "ax")
#define	stop_emulating()	__asm("clts")

#else	/* not __GNUC__ */

void	fldcw		(caddr_t addr);
void	fnclex		(void);
void	fninit		(void);
void	fnop		(void);
void	fnsave		(caddr_t addr);
void	fnstcw		(caddr_t addr);
void	fnstsw		(caddr_t addr);
void	fp_divide_by_0	(void);
void	frstor		(caddr_t addr);
#ifndef CPU_DISABLE_SSE
void	fxsave		(caddr_t addr);
void	fxrstor		(caddr_t addr);
#endif
void	start_emulating	(void);
void	stop_emulating	(void);

#endif	/* __GNUC__ */

typedef u_char bool_t;
#ifndef CPU_DISABLE_SSE
static	void	fpu_clean_state(void);
#endif


static	int	npx_attach	(device_t dev);
	void	npx_intr	(void *);
static	int	npx_probe	(device_t dev);
static	int	npx_probe1	(device_t dev);
static	void	fpusave		(union savefpu *);
static	void	fpurstor	(union savefpu *);

int	hw_float;		/* XXX currently just alias for npx_exists */

SYSCTL_INT(_hw,HW_FLOATINGPT, floatingpoint,
	CTLFLAG_RD, &hw_float, 0, 
	"Floatingpoint instructions executed in hardware");
#if (defined(I586_CPU) || defined(I686_CPU)) && !defined(CPU_DISABLE_SSE)
int mmxopt = 1;
SYSCTL_INT(_kern, OID_AUTO, mmxopt, CTLFLAG_RD, &mmxopt, 0,
	"MMX/XMM optimized bcopy/copyin/copyout support");
#endif

#ifndef SMP
static	u_int			npx0_imask;
static	struct gate_descriptor	npx_idt_probeintr;
static	int			npx_intrno;
static	volatile u_int		npx_intrs_while_probing;
static	volatile u_int		npx_traps_while_probing;
#endif

static	bool_t			npx_ex16;
static	bool_t			npx_exists;
static	bool_t			npx_irq13;
static	int			npx_irq;	/* irq number */

#ifndef SMP
/*
 * Special interrupt handlers.  Someday intr0-intr15 will be used to count
 * interrupts.  We'll still need a special exception 16 handler.  The busy
 * latch stuff in probeintr() can be moved to npxprobe().
 */
inthand_t probeintr;
__asm("								\n\
	.text							\n\
	.p2align 2,0x90						\n\
	.type	" __XSTRING(CNAME(probeintr)) ",@function	\n\
" __XSTRING(CNAME(probeintr)) ":				\n\
	ss							\n\
	incl	" __XSTRING(CNAME(npx_intrs_while_probing)) "	\n\
	pushl	%eax						\n\
	movb	$0x20,%al	# EOI (asm in strings loses cpp features) \n\
	outb	%al,$0xa0	# IO_ICU2			\n\
	outb	%al,$0x20	# IO_ICU1			\n\
	movb	$0,%al						\n\
	outb	%al,$0xf0	# clear BUSY# latch		\n\
	popl	%eax						\n\
	iret							\n\
");

inthand_t probetrap;
__asm("								\n\
	.text							\n\
	.p2align 2,0x90						\n\
	.type	" __XSTRING(CNAME(probetrap)) ",@function	\n\
" __XSTRING(CNAME(probetrap)) ":				\n\
	ss							\n\
	incl	" __XSTRING(CNAME(npx_traps_while_probing)) "	\n\
	fnclex							\n\
	iret							\n\
");
#endif /* SMP */

static struct krate badfprate = { 1 };

/*
 * Probe routine.  Initialize cr0 to give correct behaviour for [f]wait
 * whether the device exists or not (XXX should be elsewhere).  Set flags
 * to tell npxattach() what to do.  Modify device struct if npx doesn't
 * need to use interrupts.  Return 1 if device exists.
 */
static int
npx_probe(device_t dev)
{
#ifdef SMP

	if (resource_int_value("npx", 0, "irq", &npx_irq) != 0)
		npx_irq = 13;
	return npx_probe1(dev);

#else /* SMP */

	int	result;
	u_long	save_eflags;
	u_char	save_icu1_mask;
	u_char	save_icu2_mask;
	struct	gate_descriptor save_idt_npxintr;
	struct	gate_descriptor save_idt_npxtrap;
	/*
	 * This routine is now just a wrapper for npxprobe1(), to install
	 * special npx interrupt and trap handlers, to enable npx interrupts
	 * and to disable other interrupts.  Someday isa_configure() will
	 * install suitable handlers and run with interrupts enabled so we
	 * won't need to do so much here.
	 */
	if (resource_int_value("npx", 0, "irq", &npx_irq) != 0)
		npx_irq = 13;
	npx_intrno = IDT_OFFSET + npx_irq;
	save_eflags = read_eflags();
	cpu_disable_intr();
	save_icu1_mask = inb(IO_ICU1 + 1);
	save_icu2_mask = inb(IO_ICU2 + 1);
	save_idt_npxintr = idt[npx_intrno];
	save_idt_npxtrap = idt[16];
	outb(IO_ICU1 + 1, ~(1 << ICU_IRQ_SLAVE));
	outb(IO_ICU2 + 1, ~(1 << (npx_irq - 8)));
	setidt(16, probetrap, SDT_SYS386TGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	setidt(npx_intrno, probeintr, SDT_SYS386IGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	npx_idt_probeintr = idt[npx_intrno];
	cpu_enable_intr();
	result = npx_probe1(dev);
	cpu_disable_intr();
	outb(IO_ICU1 + 1, save_icu1_mask);
	outb(IO_ICU2 + 1, save_icu2_mask);
	idt[npx_intrno] = save_idt_npxintr;
	idt[16] = save_idt_npxtrap;
	write_eflags(save_eflags);
	return (result);

#endif /* SMP */
}

static int
npx_probe1(device_t dev)
{
#ifndef SMP
	u_short control;
	u_short status;
#endif

	/*
	 * Partially reset the coprocessor, if any.  Some BIOS's don't reset
	 * it after a warm boot.
	 */
	outb(0xf1, 0);		/* full reset on some systems, NOP on others */
	outb(0xf0, 0);		/* clear BUSY# latch */
	/*
	 * Prepare to trap all ESC (i.e., NPX) instructions and all WAIT
	 * instructions.  We must set the CR0_MP bit and use the CR0_TS
	 * bit to control the trap, because setting the CR0_EM bit does
	 * not cause WAIT instructions to trap.  It's important to trap
	 * WAIT instructions - otherwise the "wait" variants of no-wait
	 * control instructions would degenerate to the "no-wait" variants
	 * after FP context switches but work correctly otherwise.  It's
	 * particularly important to trap WAITs when there is no NPX -
	 * otherwise the "wait" variants would always degenerate.
	 *
	 * Try setting CR0_NE to get correct error reporting on 486DX's.
	 * Setting it should fail or do nothing on lesser processors.
	 */
	load_cr0(rcr0() | CR0_MP | CR0_NE);
	/*
	 * But don't trap while we're probing.
	 */
	stop_emulating();
	/*
	 * Finish resetting the coprocessor, if any.  If there is an error
	 * pending, then we may get a bogus IRQ13, but probeintr() will handle
	 * it OK.  Bogus halts have never been observed, but we enabled
	 * IRQ13 and cleared the BUSY# latch early to handle them anyway.
	 */
	fninit();

	device_set_desc(dev, "math processor");
	/*
	 * Modern CPUs all have an FPU that uses the INT16 interface
	 * and provide a simple way to verify that, so handle the
	 * common case right away.
	 */
	if (cpu_feature & CPUID_FPU) {
		npx_irq13 = 0;
		npx_ex16 = hw_float = npx_exists = 1;
		return (0);
	}

#ifndef SMP
	/*
	 * Don't use fwait here because it might hang.
	 * Don't use fnop here because it usually hangs if there is no FPU.
	 */
	DELAY(1000);		/* wait for any IRQ13 */
#ifdef DIAGNOSTIC
	if (npx_intrs_while_probing != 0)
		kprintf("fninit caused %u bogus npx interrupt(s)\n",
		       npx_intrs_while_probing);
	if (npx_traps_while_probing != 0)
		kprintf("fninit caused %u bogus npx trap(s)\n",
		       npx_traps_while_probing);
#endif
	/*
	 * Check for a status of mostly zero.
	 */
	status = 0x5a5a;
	fnstsw(&status);
	if ((status & 0xb8ff) == 0) {
		/*
		 * Good, now check for a proper control word.
		 */
		control = 0x5a5a;
		fnstcw(&control);
		if ((control & 0x1f3f) == 0x033f) {
			hw_float = npx_exists = 1;
			/*
			 * We have an npx, now divide by 0 to see if exception
			 * 16 works.
			 */
			control &= ~(1 << 2);	/* enable divide by 0 trap */
			fldcw(&control);
			npx_traps_while_probing = npx_intrs_while_probing = 0;
			fp_divide_by_0();
			if (npx_traps_while_probing != 0) {
				/*
				 * Good, exception 16 works.
				 */
				npx_ex16 = 1;
				return (0);
			}
			if (npx_intrs_while_probing != 0) {
				int	rid;
				struct	resource *r;
				void	*intr;
				/*
				 * Bad, we are stuck with IRQ13.
				 */
				npx_irq13 = 1;
				/*
				 * npxattach would be too late to set npx0_imask
				 */
				npx0_imask |= (1 << npx_irq);

				/*
				 * We allocate these resources permanently,
				 * so there is no need to keep track of them.
				 */
				rid = 0;
				r = bus_alloc_resource(dev, SYS_RES_IOPORT,
						       &rid, IO_NPX, IO_NPX,
						       IO_NPXSIZE, RF_ACTIVE);
				if (r == 0)
					panic("npx: can't get ports");
				rid = 0;
				r = bus_alloc_resource(dev, SYS_RES_IRQ,
						       &rid, npx_irq, npx_irq,
						       1, RF_ACTIVE);
				if (r == 0)
					panic("npx: can't get IRQ");
				BUS_SETUP_INTR(device_get_parent(dev),
					       dev, r, 0,
					       npx_intr, 0, &intr, NULL);
				if (intr == 0)
					panic("npx: can't create intr");

				return (0);
			}
			/*
			 * Worse, even IRQ13 is broken.  Use emulator.
			 */
		}
	}
#endif /* SMP */
	/*
	 * Probe failed, but we want to get to npxattach to initialize the
	 * emulator and say that it has been installed.  XXX handle devices
	 * that aren't really devices better.
	 */
	return (0);
}

/*
 * Attach routine - announce which it is, and wire into system
 */
int
npx_attach(device_t dev)
{
	int flags;

	if (resource_int_value("npx", 0, "flags", &flags) != 0)
		flags = 0;

	if (flags)
		device_printf(dev, "flags 0x%x ", flags);
	if (npx_irq13) {
		device_printf(dev, "using IRQ 13 interface\n");
	} else {
#if defined(MATH_EMULATE)
		if (npx_ex16) {
			if (!(flags & NPX_PREFER_EMULATOR))
				device_printf(dev, "INT 16 interface\n");
			else {
				device_printf(dev, "FPU exists, but flags request "
				    "emulator\n");
				hw_float = npx_exists = 0;
			}
		} else if (npx_exists) {
			device_printf(dev, "error reporting broken; using 387 emulator\n");
			hw_float = npx_exists = 0;
		} else
			device_printf(dev, "387 emulator\n");
#else
		if (npx_ex16) {
			device_printf(dev, "INT 16 interface\n");
			if (flags & NPX_PREFER_EMULATOR) {
				device_printf(dev, "emulator requested, but none compiled "
				    "into kernel, using FPU\n");
			}
		} else
			device_printf(dev, "no 387 emulator in kernel and no FPU!\n");
#endif
	}
	npxinit(__INITIAL_NPXCW__);

#if (defined(I586_CPU) || defined(I686_CPU)) && !defined(CPU_DISABLE_SSE)
	/*
	 * The asm_mmx_*() routines actually use XMM as well, so only 
	 * enable them if we have SSE2 and are using FXSR (fxsave/fxrstore).
	 */
	TUNABLE_INT_FETCH("kern.mmxopt", &mmxopt);
	if ((cpu_feature & CPUID_MMX) && (cpu_feature & CPUID_SSE) &&
	    (cpu_feature & CPUID_SSE2) && 
	    npx_ex16 && npx_exists && mmxopt && cpu_fxsr
	) {
		if ((flags & NPX_DISABLE_I586_OPTIMIZED_BCOPY) == 0) {
			bcopy_vector = (void **)asm_xmm_bcopy;
			ovbcopy_vector = (void **)asm_xmm_bcopy;
			memcpy_vector = (void **)asm_xmm_memcpy;
			kprintf("Using XMM optimized bcopy/copyin/copyout\n");
		}
		if ((flags & NPX_DISABLE_I586_OPTIMIZED_BZERO) == 0) {
			/* XXX */
		}
	} else if ((cpu_feature & CPUID_MMX) && (cpu_feature & CPUID_SSE) &&
	    npx_ex16 && npx_exists && mmxopt && cpu_fxsr
	) {
		if ((flags & NPX_DISABLE_I586_OPTIMIZED_BCOPY) == 0) {
			bcopy_vector = (void **)asm_mmx_bcopy;
			ovbcopy_vector = (void **)asm_mmx_bcopy;
			memcpy_vector = (void **)asm_mmx_memcpy;
			kprintf("Using MMX optimized bcopy/copyin/copyout\n");
		}
		if ((flags & NPX_DISABLE_I586_OPTIMIZED_BZERO) == 0) {
			/* XXX */
		}
	}

#endif
#if 0
	if (cpu_class == CPUCLASS_586 && npx_ex16 && npx_exists &&
	    timezero("i586_bzero()", i586_bzero) <
	    timezero("bzero()", bzero) * 4 / 5) {
		if (!(flags & NPX_DISABLE_I586_OPTIMIZED_BCOPY)) {
			bcopy_vector = i586_bcopy;
			ovbcopy_vector = i586_bcopy;
		}
		if (!(flags & NPX_DISABLE_I586_OPTIMIZED_BZERO))
			bzero_vector = i586_bzero;
		if (!(flags & NPX_DISABLE_I586_OPTIMIZED_COPYIO)) {
			copyin_vector = i586_copyin;
			copyout_vector = i586_copyout;
		}
	}
#endif
	return (0);		/* XXX unused */
}

/*
 * Initialize the floating point unit.
 */
void
npxinit(u_short control)
{
	static union savefpu dummy __aligned(16);

	if (!npx_exists)
		return;
	/*
	 * fninit has the same h/w bugs as fnsave.  Use the detoxified
	 * fnsave to throw away any junk in the fpu.  npxsave() initializes
	 * the fpu and sets npxthread = NULL as important side effects.
	 */
	npxsave(&dummy);
	crit_enter();
	stop_emulating();
	fldcw(&control);
	fpusave(curthread->td_savefpu);
	mdcpu->gd_npxthread = NULL;
	start_emulating();
	crit_exit();
}

/*
 * Free coprocessor (if we have it).
 */
void
npxexit(void)
{
	if (curthread == mdcpu->gd_npxthread)
		npxsave(curthread->td_savefpu);
#ifdef NPX_DEBUG
	if (npx_exists) {
		u_int	masked_exceptions;

		masked_exceptions = 
		    curthread->td_savefpu->sv_87.sv_env.en_cw
		    & curthread->td_savefpu->sv_87.sv_env.en_sw & 0x7f;
		/*
		 * Log exceptions that would have trapped with the old
		 * control word (overflow, divide by 0, and invalid operand).
		 */
		if (masked_exceptions & 0x0d)
			log(LOG_ERR,
	"pid %d (%s) exited with masked floating point exceptions 0x%02x\n",
			    curproc->p_pid, curproc->p_comm, masked_exceptions);
	}
#endif
}

/* 
 * The following mechanism is used to ensure that the FPE_... value
 * that is passed as a trapcode to the signal handler of the user
 * process does not have more than one bit set.
 * 
 * Multiple bits may be set if the user process modifies the control
 * word while a status word bit is already set.  While this is a sign
 * of bad coding, we have no choise than to narrow them down to one
 * bit, since we must not send a trapcode that is not exactly one of
 * the FPE_ macros.
 *
 * The mechanism has a static table with 127 entries.  Each combination
 * of the 7 FPU status word exception bits directly translates to a
 * position in this table, where a single FPE_... value is stored.
 * This FPE_... value stored there is considered the "most important"
 * of the exception bits and will be sent as the signal code.  The
 * precedence of the bits is based upon Intel Document "Numerical
 * Applications", Chapter "Special Computational Situations".
 *
 * The macro to choose one of these values does these steps: 1) Throw
 * away status word bits that cannot be masked.  2) Throw away the bits
 * currently masked in the control word, assuming the user isn't
 * interested in them anymore.  3) Reinsert status word bit 7 (stack
 * fault) if it is set, which cannot be masked but must be presered.
 * 4) Use the remaining bits to point into the trapcode table.
 *
 * The 6 maskable bits in order of their preference, as stated in the
 * above referenced Intel manual:
 * 1  Invalid operation (FP_X_INV)
 * 1a   Stack underflow
 * 1b   Stack overflow
 * 1c   Operand of unsupported format
 * 1d   SNaN operand.
 * 2  QNaN operand (not an exception, irrelavant here)
 * 3  Any other invalid-operation not mentioned above or zero divide
 *      (FP_X_INV, FP_X_DZ)
 * 4  Denormal operand (FP_X_DNML)
 * 5  Numeric over/underflow (FP_X_OFL, FP_X_UFL)
 * 6  Inexact result (FP_X_IMP) 
 */
static char fpetable[128] = {
	0,
	FPE_FLTINV,	/*  1 - INV */
	FPE_FLTUND,	/*  2 - DNML */
	FPE_FLTINV,	/*  3 - INV | DNML */
	FPE_FLTDIV,	/*  4 - DZ */
	FPE_FLTINV,	/*  5 - INV | DZ */
	FPE_FLTDIV,	/*  6 - DNML | DZ */
	FPE_FLTINV,	/*  7 - INV | DNML | DZ */
	FPE_FLTOVF,	/*  8 - OFL */
	FPE_FLTINV,	/*  9 - INV | OFL */
	FPE_FLTUND,	/*  A - DNML | OFL */
	FPE_FLTINV,	/*  B - INV | DNML | OFL */
	FPE_FLTDIV,	/*  C - DZ | OFL */
	FPE_FLTINV,	/*  D - INV | DZ | OFL */
	FPE_FLTDIV,	/*  E - DNML | DZ | OFL */
	FPE_FLTINV,	/*  F - INV | DNML | DZ | OFL */
	FPE_FLTUND,	/* 10 - UFL */
	FPE_FLTINV,	/* 11 - INV | UFL */
	FPE_FLTUND,	/* 12 - DNML | UFL */
	FPE_FLTINV,	/* 13 - INV | DNML | UFL */
	FPE_FLTDIV,	/* 14 - DZ | UFL */
	FPE_FLTINV,	/* 15 - INV | DZ | UFL */
	FPE_FLTDIV,	/* 16 - DNML | DZ | UFL */
	FPE_FLTINV,	/* 17 - INV | DNML | DZ | UFL */
	FPE_FLTOVF,	/* 18 - OFL | UFL */
	FPE_FLTINV,	/* 19 - INV | OFL | UFL */
	FPE_FLTUND,	/* 1A - DNML | OFL | UFL */
	FPE_FLTINV,	/* 1B - INV | DNML | OFL | UFL */
	FPE_FLTDIV,	/* 1C - DZ | OFL | UFL */
	FPE_FLTINV,	/* 1D - INV | DZ | OFL | UFL */
	FPE_FLTDIV,	/* 1E - DNML | DZ | OFL | UFL */
	FPE_FLTINV,	/* 1F - INV | DNML | DZ | OFL | UFL */
	FPE_FLTRES,	/* 20 - IMP */
	FPE_FLTINV,	/* 21 - INV | IMP */
	FPE_FLTUND,	/* 22 - DNML | IMP */
	FPE_FLTINV,	/* 23 - INV | DNML | IMP */
	FPE_FLTDIV,	/* 24 - DZ | IMP */
	FPE_FLTINV,	/* 25 - INV | DZ | IMP */
	FPE_FLTDIV,	/* 26 - DNML | DZ | IMP */
	FPE_FLTINV,	/* 27 - INV | DNML | DZ | IMP */
	FPE_FLTOVF,	/* 28 - OFL | IMP */
	FPE_FLTINV,	/* 29 - INV | OFL | IMP */
	FPE_FLTUND,	/* 2A - DNML | OFL | IMP */
	FPE_FLTINV,	/* 2B - INV | DNML | OFL | IMP */
	FPE_FLTDIV,	/* 2C - DZ | OFL | IMP */
	FPE_FLTINV,	/* 2D - INV | DZ | OFL | IMP */
	FPE_FLTDIV,	/* 2E - DNML | DZ | OFL | IMP */
	FPE_FLTINV,	/* 2F - INV | DNML | DZ | OFL | IMP */
	FPE_FLTUND,	/* 30 - UFL | IMP */
	FPE_FLTINV,	/* 31 - INV | UFL | IMP */
	FPE_FLTUND,	/* 32 - DNML | UFL | IMP */
	FPE_FLTINV,	/* 33 - INV | DNML | UFL | IMP */
	FPE_FLTDIV,	/* 34 - DZ | UFL | IMP */
	FPE_FLTINV,	/* 35 - INV | DZ | UFL | IMP */
	FPE_FLTDIV,	/* 36 - DNML | DZ | UFL | IMP */
	FPE_FLTINV,	/* 37 - INV | DNML | DZ | UFL | IMP */
	FPE_FLTOVF,	/* 38 - OFL | UFL | IMP */
	FPE_FLTINV,	/* 39 - INV | OFL | UFL | IMP */
	FPE_FLTUND,	/* 3A - DNML | OFL | UFL | IMP */
	FPE_FLTINV,	/* 3B - INV | DNML | OFL | UFL | IMP */
	FPE_FLTDIV,	/* 3C - DZ | OFL | UFL | IMP */
	FPE_FLTINV,	/* 3D - INV | DZ | OFL | UFL | IMP */
	FPE_FLTDIV,	/* 3E - DNML | DZ | OFL | UFL | IMP */
	FPE_FLTINV,	/* 3F - INV | DNML | DZ | OFL | UFL | IMP */
	FPE_FLTSUB,	/* 40 - STK */
	FPE_FLTSUB,	/* 41 - INV | STK */
	FPE_FLTUND,	/* 42 - DNML | STK */
	FPE_FLTSUB,	/* 43 - INV | DNML | STK */
	FPE_FLTDIV,	/* 44 - DZ | STK */
	FPE_FLTSUB,	/* 45 - INV | DZ | STK */
	FPE_FLTDIV,	/* 46 - DNML | DZ | STK */
	FPE_FLTSUB,	/* 47 - INV | DNML | DZ | STK */
	FPE_FLTOVF,	/* 48 - OFL | STK */
	FPE_FLTSUB,	/* 49 - INV | OFL | STK */
	FPE_FLTUND,	/* 4A - DNML | OFL | STK */
	FPE_FLTSUB,	/* 4B - INV | DNML | OFL | STK */
	FPE_FLTDIV,	/* 4C - DZ | OFL | STK */
	FPE_FLTSUB,	/* 4D - INV | DZ | OFL | STK */
	FPE_FLTDIV,	/* 4E - DNML | DZ | OFL | STK */
	FPE_FLTSUB,	/* 4F - INV | DNML | DZ | OFL | STK */
	FPE_FLTUND,	/* 50 - UFL | STK */
	FPE_FLTSUB,	/* 51 - INV | UFL | STK */
	FPE_FLTUND,	/* 52 - DNML | UFL | STK */
	FPE_FLTSUB,	/* 53 - INV | DNML | UFL | STK */
	FPE_FLTDIV,	/* 54 - DZ | UFL | STK */
	FPE_FLTSUB,	/* 55 - INV | DZ | UFL | STK */
	FPE_FLTDIV,	/* 56 - DNML | DZ | UFL | STK */
	FPE_FLTSUB,	/* 57 - INV | DNML | DZ | UFL | STK */
	FPE_FLTOVF,	/* 58 - OFL | UFL | STK */
	FPE_FLTSUB,	/* 59 - INV | OFL | UFL | STK */
	FPE_FLTUND,	/* 5A - DNML | OFL | UFL | STK */
	FPE_FLTSUB,	/* 5B - INV | DNML | OFL | UFL | STK */
	FPE_FLTDIV,	/* 5C - DZ | OFL | UFL | STK */
	FPE_FLTSUB,	/* 5D - INV | DZ | OFL | UFL | STK */
	FPE_FLTDIV,	/* 5E - DNML | DZ | OFL | UFL | STK */
	FPE_FLTSUB,	/* 5F - INV | DNML | DZ | OFL | UFL | STK */
	FPE_FLTRES,	/* 60 - IMP | STK */
	FPE_FLTSUB,	/* 61 - INV | IMP | STK */
	FPE_FLTUND,	/* 62 - DNML | IMP | STK */
	FPE_FLTSUB,	/* 63 - INV | DNML | IMP | STK */
	FPE_FLTDIV,	/* 64 - DZ | IMP | STK */
	FPE_FLTSUB,	/* 65 - INV | DZ | IMP | STK */
	FPE_FLTDIV,	/* 66 - DNML | DZ | IMP | STK */
	FPE_FLTSUB,	/* 67 - INV | DNML | DZ | IMP | STK */
	FPE_FLTOVF,	/* 68 - OFL | IMP | STK */
	FPE_FLTSUB,	/* 69 - INV | OFL | IMP | STK */
	FPE_FLTUND,	/* 6A - DNML | OFL | IMP | STK */
	FPE_FLTSUB,	/* 6B - INV | DNML | OFL | IMP | STK */
	FPE_FLTDIV,	/* 6C - DZ | OFL | IMP | STK */
	FPE_FLTSUB,	/* 6D - INV | DZ | OFL | IMP | STK */
	FPE_FLTDIV,	/* 6E - DNML | DZ | OFL | IMP | STK */
	FPE_FLTSUB,	/* 6F - INV | DNML | DZ | OFL | IMP | STK */
	FPE_FLTUND,	/* 70 - UFL | IMP | STK */
	FPE_FLTSUB,	/* 71 - INV | UFL | IMP | STK */
	FPE_FLTUND,	/* 72 - DNML | UFL | IMP | STK */
	FPE_FLTSUB,	/* 73 - INV | DNML | UFL | IMP | STK */
	FPE_FLTDIV,	/* 74 - DZ | UFL | IMP | STK */
	FPE_FLTSUB,	/* 75 - INV | DZ | UFL | IMP | STK */
	FPE_FLTDIV,	/* 76 - DNML | DZ | UFL | IMP | STK */
	FPE_FLTSUB,	/* 77 - INV | DNML | DZ | UFL | IMP | STK */
	FPE_FLTOVF,	/* 78 - OFL | UFL | IMP | STK */
	FPE_FLTSUB,	/* 79 - INV | OFL | UFL | IMP | STK */
	FPE_FLTUND,	/* 7A - DNML | OFL | UFL | IMP | STK */
	FPE_FLTSUB,	/* 7B - INV | DNML | OFL | UFL | IMP | STK */
	FPE_FLTDIV,	/* 7C - DZ | OFL | UFL | IMP | STK */
	FPE_FLTSUB,	/* 7D - INV | DZ | OFL | UFL | IMP | STK */
	FPE_FLTDIV,	/* 7E - DNML | DZ | OFL | UFL | IMP | STK */
	FPE_FLTSUB,	/* 7F - INV | DNML | DZ | OFL | UFL | IMP | STK */
};

/*
 * Preserve the FP status word, clear FP exceptions, then generate a SIGFPE.
 *
 * Clearing exceptions is necessary mainly to avoid IRQ13 bugs.  We now
 * depend on longjmp() restoring a usable state.  Restoring the state
 * or examining it might fail if we didn't clear exceptions.
 *
 * The error code chosen will be one of the FPE_... macros. It will be
 * sent as the second argument to old BSD-style signal handlers and as
 * "siginfo_t->si_code" (second argument) to SA_SIGINFO signal handlers.
 *
 * XXX the FP state is not preserved across signal handlers.  So signal
 * handlers cannot afford to do FP unless they preserve the state or
 * longjmp() out.  Both preserving the state and longjmp()ing may be
 * destroyed by IRQ13 bugs.  Clearing FP exceptions is not an acceptable
 * solution for signals other than SIGFPE.
 *
 * The MP lock is not held on entry (see i386/i386/exception.s) and
 * should not be held on exit.  Interrupts are enabled.  We must enter
 * a critical section to stabilize the FP system and prevent an interrupt
 * or preemption from changing the FP state out from under us.
 */
void
npx_intr(void *dummy)
{
	int code;
	u_short control;
	u_short status;
	struct intrframe *frame;

	crit_enter();

	/*
	 * This exception can only occur with CR0_TS clear, otherwise we
	 * would get a DNA exception.  However, since interrupts were
	 * enabled a preemption could have sneaked in and used the FP system
	 * before we entered our critical section.  If that occured, the
	 * TS bit will be set and npxthread will be NULL.
	 */
	if (npx_exists && (rcr0() & CR0_TS)) {
		KASSERT(mdcpu->gd_npxthread == NULL, ("gd_npxthread was %p with TS set!", mdcpu->gd_npxthread));
		npxdna();
		crit_exit();
		return;
	}
	if (mdcpu->gd_npxthread == NULL || !npx_exists) {
		get_mplock();
		kprintf("npxintr: npxthread = %p, curthread = %p, npx_exists = %d\n",
		       mdcpu->gd_npxthread, curthread, npx_exists);
		panic("npxintr from nowhere");
	}
	if (mdcpu->gd_npxthread != curthread) {
		get_mplock();
		kprintf("npxintr: npxthread = %p, curthread = %p, npx_exists = %d\n",
		       mdcpu->gd_npxthread, curthread, npx_exists);
		panic("npxintr from non-current process");
	}

	outb(0xf0, 0);
	fnstsw(&status);
	fnstcw(&control);
	fnclex();

	get_mplock();

	/*
	 * Pass exception to process.
	 */
	frame = (struct intrframe *)&dummy;	/* XXX */
	if ((ISPL(frame->if_cs) == SEL_UPL) || (frame->if_eflags & PSL_VM)) {
		/*
		 * Interrupt is essentially a trap, so we can afford to call
		 * the SIGFPE handler (if any) as soon as the interrupt
		 * returns.
		 *
		 * XXX little or nothing is gained from this, and plenty is
		 * lost - the interrupt frame has to contain the trap frame
		 * (this is otherwise only necessary for the rescheduling trap
		 * in doreti, and the frame for that could easily be set up
		 * just before it is used).
		 */
		curthread->td_lwp->lwp_md.md_regs = INTR_TO_TRAPFRAME(frame);
		/*
		 * Encode the appropriate code for detailed information on
		 * this exception.
		 */
		code = 
		    fpetable[(status & ~control & 0x3f) | (status & 0x40)];
		trapsignal(curthread->td_lwp, SIGFPE, code);
	} else {
		/*
		 * Nested interrupt.  These losers occur when:
		 *	o an IRQ13 is bogusly generated at a bogus time, e.g.:
		 *		o immediately after an fnsave or frstor of an
		 *		  error state.
		 *		o a couple of 386 instructions after
		 *		  "fstpl _memvar" causes a stack overflow.
		 *	  These are especially nasty when combined with a
		 *	  trace trap.
		 *	o an IRQ13 occurs at the same time as another higher-
		 *	  priority interrupt.
		 *
		 * Treat them like a true async interrupt.
		 */
		lwpsignal(curproc, curthread->td_lwp, SIGFPE);
	}
	rel_mplock();
	crit_exit();
}

/*
 * Implement the device not available (DNA) exception.  gd_npxthread had 
 * better be NULL.  Restore the current thread's FP state and set gd_npxthread
 * to curthread.
 *
 * Interrupts are enabled and preemption can occur.  Enter a critical
 * section to stabilize the FP state.
 */
int
npxdna(void)
{
	thread_t td = curthread;
	int didinit = 0;

	if (!npx_exists)
		return (0);
	if (mdcpu->gd_npxthread != NULL) {
		kprintf("npxdna: npxthread = %p, curthread = %p\n",
		       mdcpu->gd_npxthread, td);
		panic("npxdna");
	}

	/*
	 * Setup the initial saved state if the thread has never before
	 * used the FP unit.  This also occurs when a thread pushes a
	 * signal handler and uses FP in the handler.
	 */
	if ((td->td_flags & (TDF_USINGFP | TDF_KERNELFP)) == 0) {
		td->td_flags |= TDF_USINGFP;
		npxinit(__INITIAL_NPXCW__);
		didinit = 1;
	}

	/*
	 * The setting of gd_npxthread and the call to fpurstor() must not
	 * be preempted by an interrupt thread or we will take an npxdna
	 * trap and potentially save our current fpstate (which is garbage)
	 * and then restore the garbage rather then the originally saved
	 * fpstate.
	 */
	crit_enter();
	stop_emulating();
	/*
	 * Record new context early in case frstor causes an IRQ13.
	 */
	mdcpu->gd_npxthread = td;
	/*
	 * The following frstor may cause an IRQ13 when the state being
	 * restored has a pending error.  The error will appear to have been
	 * triggered by the current (npx) user instruction even when that
	 * instruction is a no-wait instruction that should not trigger an
	 * error (e.g., fnclex).  On at least one 486 system all of the
	 * no-wait instructions are broken the same as frstor, so our
	 * treatment does not amplify the breakage.  On at least one
	 * 386/Cyrix 387 system, fnclex works correctly while frstor and
	 * fnsave are broken, so our treatment breaks fnclex if it is the
	 * first FPU instruction after a context switch.
	 */
	if ((td->td_savefpu->sv_xmm.sv_env.en_mxcsr & ~0xFFBF)
#ifndef CPU_DISABLE_SSE
	    && cpu_fxsr
#endif
	   ) {
		krateprintf(&badfprate,
			    "FXRSTR: illegal FP MXCSR %08x didinit = %d\n",
			    td->td_savefpu->sv_xmm.sv_env.en_mxcsr, didinit);
		td->td_savefpu->sv_xmm.sv_env.en_mxcsr &= 0xFFBF;
		lwpsignal(curproc, curthread->td_lwp, SIGFPE);
	}
	fpurstor(td->td_savefpu);
	crit_exit();

	return (1);
}

/*
 * Wrapper for the fnsave instruction to handle h/w bugs.  If there is an error
 * pending, then fnsave generates a bogus IRQ13 on some systems.  Force
 * any IRQ13 to be handled immediately, and then ignore it.  This routine is
 * often called at splhigh so it must not use many system services.  In
 * particular, it's much easier to install a special handler than to
 * guarantee that it's safe to use npxintr() and its supporting code.
 *
 * WARNING!  This call is made during a switch and the MP lock will be
 * setup for the new target thread rather then the current thread, so we
 * cannot do anything here that depends on the *_mplock() functions as
 * we may trip over their assertions.
 *
 * WARNING!  When using fxsave we MUST fninit after saving the FP state.  The
 * kernel will always assume that the FP state is 'safe' (will not cause
 * exceptions) for mmx/xmm use if npxthread is NULL.  The kernel must still
 * setup a custom save area before actually using the FP unit, but it will
 * not bother calling fninit.  This greatly improves kernel performance when
 * it wishes to use the FP unit.
 */
void
npxsave(union savefpu *addr)
{
#if defined(SMP) || !defined(CPU_DISABLE_SSE)

	crit_enter();
	stop_emulating();
	fpusave(addr);
	mdcpu->gd_npxthread = NULL;
	fninit();
	start_emulating();
	crit_exit();

#else /* !SMP and CPU_DISABLE_SSE */

	u_char	icu1_mask;
	u_char	icu2_mask;
	u_char	old_icu1_mask;
	u_char	old_icu2_mask;
	struct gate_descriptor	save_idt_npxintr;
	u_long	save_eflags;

	save_eflags = read_eflags();
	cpu_disable_intr();
	old_icu1_mask = inb(IO_ICU1 + 1);
	old_icu2_mask = inb(IO_ICU2 + 1);
	save_idt_npxintr = idt[npx_intrno];
	outb(IO_ICU1 + 1, old_icu1_mask & ~((1 << ICU_IRQ_SLAVE) | npx0_imask));
	outb(IO_ICU2 + 1, old_icu2_mask & ~(npx0_imask >> 8));
	idt[npx_intrno] = npx_idt_probeintr;
	cpu_enable_intr();
	stop_emulating();
	fnsave(addr);
	fnop();
	cpu_disable_intr();
	mdcpu->gd_npxthread = NULL;
	start_emulating();
	icu1_mask = inb(IO_ICU1 + 1);	/* masks may have changed */
	icu2_mask = inb(IO_ICU2 + 1);
	outb(IO_ICU1 + 1,
	     (icu1_mask & ~npx0_imask) | (old_icu1_mask & npx0_imask));
	outb(IO_ICU2 + 1,
	     (icu2_mask & ~(npx0_imask >> 8))
	     | (old_icu2_mask & (npx0_imask >> 8)));
	idt[npx_intrno] = save_idt_npxintr;
	write_eflags(save_eflags); 	/* back to usual state */

#endif /* SMP */
}

static void
fpusave(union savefpu *addr)
{
#ifndef CPU_DISABLE_SSE
	if (cpu_fxsr)
		fxsave(addr);
	else
#endif
		fnsave(addr);
}

/*
 * Save the FP state to the mcontext structure.
 *
 * WARNING: If you want to try to npxsave() directly to mctx->mc_fpregs,
 * then it MUST be 16-byte aligned.  Currently this is not guarenteed.
 */
void
npxpush(mcontext_t *mctx)
{
	thread_t td = curthread;

	KKASSERT((td->td_flags & TDF_KERNELFP) == 0);

	if (td->td_flags & TDF_USINGFP) {
		if (mdcpu->gd_npxthread == td) {
			/*
			 * XXX Note: This is a bit inefficient if the signal
			 * handler uses floating point, extra faults will
			 * occur.
			 */
			mctx->mc_ownedfp = _MC_FPOWNED_FPU;
			npxsave(td->td_savefpu);
		} else {
			mctx->mc_ownedfp = _MC_FPOWNED_PCB;
		}
		bcopy(td->td_savefpu, mctx->mc_fpregs, sizeof(mctx->mc_fpregs));
		td->td_flags &= ~TDF_USINGFP;
		mctx->mc_fpformat =
#ifndef CPU_DISABLE_SSE
			(cpu_fxsr) ? _MC_FPFMT_XMM :
#endif
			_MC_FPFMT_387;
	} else {
		mctx->mc_ownedfp = _MC_FPOWNED_NONE;
		mctx->mc_fpformat = _MC_FPFMT_NODEV;
	}
}

/*
 * Restore the FP state from the mcontext structure.
 */
void
npxpop(mcontext_t *mctx)
{
	thread_t td = curthread;

	KKASSERT((td->td_flags & TDF_KERNELFP) == 0);

	switch(mctx->mc_ownedfp) {
	case _MC_FPOWNED_NONE:
		/*
		 * If the signal handler used the FP unit but the interrupted
		 * code did not, release the FP unit.  Clear TDF_USINGFP will
		 * force the FP unit to reinit so the interrupted code sees
		 * a clean slate.
		 */
		if (td->td_flags & TDF_USINGFP) {
			if (td == mdcpu->gd_npxthread)
				npxsave(td->td_savefpu);
			td->td_flags &= ~TDF_USINGFP;
		}
		break;
	case _MC_FPOWNED_FPU:
	case _MC_FPOWNED_PCB:
		/*
		 * Clear ownership of the FP unit and restore our saved state.
		 *
		 * NOTE: The signal handler may have set-up some FP state and
		 * enabled the FP unit, so we have to restore no matter what.
		 *
		 * XXX: This is bit inefficient, if the code being returned
		 * to is actively using the FP this results in multiple
		 * kernel faults.
		 *
		 * WARNING: The saved state was exposed to userland and may
		 * have to be sanitized to avoid a GP fault in the kernel.
		 */
		if (td == mdcpu->gd_npxthread)
			npxsave(td->td_savefpu);
		bcopy(mctx->mc_fpregs, td->td_savefpu, sizeof(*td->td_savefpu));
		if ((td->td_savefpu->sv_xmm.sv_env.en_mxcsr & ~0xFFBF)
#ifndef CPU_DISABLE_SSE
		    && cpu_fxsr
#endif
		   ) {
			krateprintf(&badfprate,
				    "pid %d (%s) signal return from user: "
				    "illegal FP MXCSR %08x\n",
				    td->td_proc->p_pid,
				    td->td_proc->p_comm,
				    td->td_savefpu->sv_xmm.sv_env.en_mxcsr);
			td->td_savefpu->sv_xmm.sv_env.en_mxcsr &= 0xFFBF;
		}
		td->td_flags |= TDF_USINGFP;
		break;
	}
}

#ifndef CPU_DISABLE_SSE
/*
 * On AuthenticAMD processors, the fxrstor instruction does not restore
 * the x87's stored last instruction pointer, last data pointer, and last
 * opcode values, except in the rare case in which the exception summary
 * (ES) bit in the x87 status word is set to 1.
 *
 * In order to avoid leaking this information across processes, we clean
 * these values by performing a dummy load before executing fxrstor().
 */
static	double	dummy_variable = 0.0;
static void
fpu_clean_state(void)
{
	u_short status;

	/*
	 * Clear the ES bit in the x87 status word if it is currently
	 * set, in order to avoid causing a fault in the upcoming load.
	 */
	fnstsw(&status);
	if (status & 0x80)
		fnclex();

	/*
	 * Load the dummy variable into the x87 stack.  This mangles
	 * the x87 stack, but we don't care since we're about to call
	 * fxrstor() anyway.
	 */
	__asm __volatile("ffree %%st(7); fld %0" : : "m" (dummy_variable));
}
#endif /* CPU_DISABLE_SSE */

static void
fpurstor(union savefpu *addr)
{
#ifndef CPU_DISABLE_SSE
	if (cpu_fxsr) {
		fpu_clean_state();
		fxrstor(addr);
	} else {
		frstor(addr);
	}
#else
	frstor(addr);
#endif
}

/*
 * Because npx is a static device that always exists under nexus,
 * and is not scanned by the nexus device, we need an identify
 * function to install the device.
 */
static device_method_t npx_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	bus_generic_identify),
	DEVMETHOD(device_probe,		npx_probe),
	DEVMETHOD(device_attach,	npx_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	
	{ 0, 0 }
};

static driver_t npx_driver = {
	"npx",
	npx_methods,
	1,			/* no softc */
};

static devclass_t npx_devclass;

/*
 * We prefer to attach to the root nexus so that the usual case (exception 16)
 * doesn't describe the processor as being `on isa'.
 */
DRIVER_MODULE(npx, nexus, npx_driver, npx_devclass, 0, 0);
