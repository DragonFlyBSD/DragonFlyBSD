/*-
 * Copyright (c) 1989, 1990 William F. Jolitz.
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2007 The FreeBSD Foundation
 * Copyright (c) 2008 The DragonFly Project.
 * Copyright (c) 2008 Jordan Gordeev.
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 */

#if 0 /* JG */
#include "opt_atpic.h"
#include "opt_compat.h"
#endif

#include <machine/asmacros.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/segments.h>

#include "assym.s"

	.text

	.globl	lwkt_switch_return

/*****************************************************************************/
/* Trap handling                                                             */
/*****************************************************************************/
/*
 * Trap and fault vector routines.
 *
 * All traps are 'interrupt gates', SDT_SYSIGT.  An interrupt gate pushes
 * state on the stack but also disables interrupts.  This is important for
 * us for the use of the swapgs instruction.  We cannot be interrupted
 * until the GS.base value is correct.  For most traps, we automatically
 * then enable interrupts if the interrupted context had them enabled.
 *
 * The cpu will push a certain amount of state onto the kernel stack for
 * the current process.  See x86_64/include/frame.h.
 * This includes the current RFLAGS (status register, which includes
 * the interrupt disable state prior to the trap), the code segment register,
 * and the return instruction pointer are pushed by the cpu.  The cpu
 * will also push an 'error' code for certain traps.  We push a dummy
 * error code for those traps where the cpu doesn't in order to maintain
 * a consistent frame.  We also push a contrived 'trap number'.
 *
 * The cpu does not push the general registers, we must do that, and we
 * must restore them prior to calling 'iret'.  The cpu adjusts the %cs and
 * %ss segment registers, but does not mess with %ds, %es, or %fs.  Thus we
 * must load them with appropriate values for supervisor mode operation.
 */

MCOUNT_LABEL(user)
MCOUNT_LABEL(btrap)

/*
 * Interrupts must be disabled for all traps, otherwise horrible %gs
 * issues will occur.
 */

/* Regular traps; The cpu does not supply tf_err for these. */
#define	TRAP(a)	 \
	PUSH_FRAME_TFRIP ;			\
	movq $0,TF_XFLAGS(%rsp) ;		\
	movq $(a),TF_TRAPNO(%rsp) ;		\
	movq $0,TF_ADDR(%rsp) ;			\
	movq $0,TF_ERR(%rsp) ;			\
	jmp alltraps

/* This group of traps have tf_err already pushed by the cpu */
#define	TRAP_ERR(a)				\
	PUSH_FRAME_TFERR ;			\
	movq $(a),TF_TRAPNO(%rsp) ;		\
	movq $0,TF_ADDR(%rsp) ;			\
	movq $0,TF_XFLAGS(%rsp) ;		\
	jmp alltraps

IDTVEC(dbg)
	TRAP(T_TRCTRAP)
IDTVEC(bpt)
	TRAP(T_BPTFLT)
IDTVEC(div)
	TRAP(T_DIVIDE)
IDTVEC(ofl)
	TRAP(T_OFLOW)
IDTVEC(bnd)
	TRAP(T_BOUND)
IDTVEC(ill)
	TRAP(T_PRIVINFLT)
IDTVEC(dna)
	TRAP(T_DNA)
IDTVEC(fpusegm)
	TRAP(T_FPOPFLT)
IDTVEC(mchk)
	TRAP(T_MCHK)
IDTVEC(rsvd)
	TRAP(T_RESERVED)
IDTVEC(fpu)
	TRAP(T_ARITHTRAP)
IDTVEC(xmm)
	TRAP(T_XMMFLT)

IDTVEC(tss)
	TRAP_ERR(T_TSSFLT)
IDTVEC(missing)
	TRAP_ERR(T_SEGNPFLT)
IDTVEC(stk)
	TRAP_ERR(T_STKFLT)
IDTVEC(align)
	TRAP_ERR(T_ALIGNFLT)

	/*
	 * alltraps entry point.  Use swapgs if this is the first time in the
	 * kernel from userland.  Reenable interrupts if they were enabled
	 * before the trap.
	 *
	 * WARNING!  %gs not available until after our swapgs code
	 */
	SUPERALIGN_TEXT
	.globl	alltraps
	.type	alltraps,@function
alltraps:

#if 0
alltraps_pushregs:
	movq	%rdi,TF_RDI(%rsp)
alltraps_pushregs_no_rdi:
	movq	%rsi,TF_RSI(%rsp)
	movq	%rdx,TF_RDX(%rsp)
	movq	%rcx,TF_RCX(%rsp)
	movq	%r8,TF_R8(%rsp)
	movq	%r9,TF_R9(%rsp)
	movq	%rax,TF_RAX(%rsp)
	movq	%rbx,TF_RBX(%rsp)
	movq	%rbp,TF_RBP(%rsp)
	movq	%r10,TF_R10(%rsp)
	movq	%r11,TF_R11(%rsp)
	movq	%r12,TF_R12(%rsp)
	movq	%r13,TF_R13(%rsp)
	movq	%r14,TF_R14(%rsp)
	movq	%r15,TF_R15(%rsp)
#endif
	sti
	FAKE_MCOUNT(TF_RIP(%rsp))
	.globl	calltrap
	.type	calltrap,@function
calltrap:
	cld
	movq	%rsp, %rdi
	call	trap
	MEXITCOUNT
	jmp	doreti			/* Handle any pending ASTs */

IDTVEC(dblfault)
	PUSH_FRAME_TFERR
	movq	$T_DOUBLEFLT,TF_TRAPNO(%rsp)
	movq	$0,TF_ADDR(%rsp)
	movq	$0,TF_XFLAGS(%rsp)

	cld
	movq	%rsp, %rdi
	call	dblfault_handler
2:	hlt
	jmp	2b

	/*
	 * We need to save the contents of %cr2 before PUSH_FRAME* messes
	 * with %cr3.
	 */
IDTVEC(page)
	PUSH_FRAME_TFERR_SAVECR2
	movq	$T_PAGEFLT,TF_TRAPNO(%rsp)
	movq	$0,TF_XFLAGS(%rsp)
	jmp	alltraps

	/*
	 * We have to special-case this one.  If we get a trap in doreti() at
	 * the iretq stage, we'll reenter as a kernel exception with the
	 * wrong gs and isolation state.  We have to act as through we came
	 * in from userland.
	 */
IDTVEC(prot)
	pushq	%r10
	leaq	doreti_iret(%rip),%r10
	cmpq	%r10,TF_RIP-TF_ERR+8(%rsp)		/* +8 due to pushq */
	jne	prot_normal
	testb	$SEL_RPL_MASK,TF_CS-TF_ERR+8(%rsp)      /* +8 due to pushq */
	jnz	prot_normal

	/*
	 * Special fault during iretq
	 */
	popq	%r10
	swapgs
	KMMUENTER_TFERR
	subq	$TF_ERR,%rsp
	PUSH_FRAME_REGS
	movq	$T_PROTFLT,TF_TRAPNO(%rsp)
	movq	$0,TF_ADDR(%rsp)
	movq	$0,TF_XFLAGS(%rsp)
	jmp	alltraps

prot_normal:
	popq	%r10
	PUSH_FRAME_TFERR
	movq	$T_PROTFLT,TF_TRAPNO(%rsp)
	movq	$0,TF_ADDR(%rsp)
	movq	$0,TF_XFLAGS(%rsp)
	jmp	alltraps

/*
 * Fast syscall entry point.  We enter here with just our new %cs/%ss set,
 * and the new privilige level.  We are still running on the old user stack
 * pointer.  We have to juggle a few things around to find our stack etc.
 * swapgs gives us access to our PCPU space only.
 *
 * We use GD_TRAMPOLINE+TR_CR2 to save the user stack pointer temporarily.
 */
IDTVEC(fast_syscall)
	swapgs					/* get kernel %gs */
	movq	%rsp,PCPU(trampoline)+TR_CR2	/* save user %rsp */
	movq	PCPU(common_tss)+TSS_RSP0,%rsp

	/*
	 * NOTE: KMMUENTER_SYSCALL does not actually use the stack but
	 *	 adjust the stack pointer for correctness in case we
	 *	 do in the future.
	 */
	subq	$TR_PCB_RSP,%rsp
	KMMUENTER_SYSCALL
	movq	PCPU(trampoline)+TR_PCB_RSP,%rsp

	/* Now emulate a trapframe. Make the 8 byte alignment odd for call. */
	subq	$TF_SIZE,%rsp
	/* defer TF_RSP till we have a spare register */
	movq	%r11,TF_RFLAGS(%rsp)
	movq	%rcx,TF_RIP(%rsp)	/* %rcx original value is in %r10 */
	movq	PCPU(trampoline)+TR_CR2,%r11	/* %r11 already saved */
	movq	%r11,TF_RSP(%rsp)	/* user stack pointer */
	orl	$RQF_QUICKRET,PCPU(reqflags)
	movq	$KUDSEL,TF_SS(%rsp)
	movq	$KUCSEL,TF_CS(%rsp)
	movq	$2,TF_ERR(%rsp)
	movq	$T_FAST_SYSCALL,TF_TRAPNO(%rsp)	/* for the vkernel */
	movq	$0,TF_XFLAGS(%rsp)	/* note: used in signal frame */
	movq	%rdi,TF_RDI(%rsp)	/* arg 1 */
	movq	%rsi,TF_RSI(%rsp)	/* arg 2 */
	movq	%rdx,TF_RDX(%rsp)	/* arg 3 */
	movq	%r10,TF_RCX(%rsp)	/* arg 4 */
	movq	%r8,TF_R8(%rsp)		/* arg 5 */
	movq	%r9,TF_R9(%rsp)		/* arg 6 */
	movq	%rax,TF_RAX(%rsp)	/* syscall number */
	movq	%rbx,TF_RBX(%rsp)	/* C preserved */
	movq	%rbp,TF_RBP(%rsp)	/* C preserved */
	movq	%r12,TF_R12(%rsp)	/* C preserved */
	movq	%r13,TF_R13(%rsp)	/* C preserved */
	movq	%r14,TF_R14(%rsp)	/* C preserved */
	movq	%r15,TF_R15(%rsp)	/* C preserved */
	sti
	FAKE_MCOUNT(TF_RIP(%rsp))
	movq	%rsp, %rdi
	call	syscall2

	/*
	 * Fast return from system call
	 */
	cli
	testl	$RQF_IPIQ|RQF_TIMER|RQF_INTPEND|RQF_AST_MASK,PCPU(reqflags)
	jnz	1f
	testl	$RQF_QUICKRET,PCPU(reqflags)
	jz	1f
	MEXITCOUNT
	movq	TF_RDI(%rsp),%rdi
	movq	TF_RSI(%rsp),%rsi
	movq	TF_RDX(%rsp),%rdx
	movq	TF_RAX(%rsp),%rax
	movq	TF_RFLAGS(%rsp),%r11
	movq	TF_RIP(%rsp),%rcx
	movq	TF_RSP(%rsp),%rsp
	KMMUEXIT_SYSCALL
	swapgs
	sysretq

	/*
	 * Normal slow / full iret
	 */
1:
	MEXITCOUNT
	jmp	doreti

/*
 * Here for CYA insurance, in case a "syscall" instruction gets
 * issued from 32 bit compatibility mode. MSR_CSTAR has to point
 * to *something* if EFER_SCE is enabled.
 */
IDTVEC(fast_syscall32)
	sysret

/*
 * NMI handling is special.
 *
 * First, NMIs do not respect the state of the processor's RFLAGS.IF
 * bit and the NMI handler may be invoked at any time, including when
 * the processor is in a critical section with RFLAGS.IF == 0.  In
 * particular, this means that the processor's GS.base values could be
 * inconsistent on entry to the handler, and so we need to read
 * MSR_GSBASE to determine if a 'swapgs' is needed.  We use '%ebx', a
 * C-preserved register, to remember whether to swap GS back on the
 * exit path.
 *
 * Second, the processor treats NMIs specially, blocking further NMIs
 * until an 'iretq' instruction is executed.  We therefore need to
 * execute the NMI handler with interrupts disabled to prevent a
 * nested interrupt from executing an 'iretq' instruction and
 * inadvertently taking the processor out of NMI mode.
 *
 * Third, the NMI handler runs on its own stack (tss_ist1), shared
 * with the double fault handler.
 */

IDTVEC(nmi)
	PUSH_FRAME_TFRIP
	movq	$0,TF_XFLAGS(%rsp)
	movq	$T_NMI,TF_TRAPNO(%rsp)
	movq	$0,TF_ADDR(%rsp)
	movq	$0,TF_ERR(%rsp)

	FAKE_MCOUNT(TF_RIP(%rsp))
	cld
	movq	%rsp, %rdi
	call	trap
	MEXITCOUNT

	POP_FRAME(jmp doreti_iret)

/*
 * This function is what cpu_heavy_restore jumps to after a new process
 * is created.  The LWKT subsystem switches while holding a critical
 * section and we maintain that abstraction here (e.g. because
 * cpu_heavy_restore needs it due to PCB_*() manipulation), then get out of
 * it before calling the initial function (typically fork_return()) and/or
 * returning to user mode.
 *
 * The MP lock is not held at any point but the critcount is bumped
 * on entry to prevent interruption of the trampoline at a bad point.
 *
 * This is effectively what td->td_switch() returns to.  It 'returns' the
 * old thread in %rax and since this is not returning to a td->td_switch()
 * call from lwkt_switch() we must handle the cleanup for the old thread
 * by calling lwkt_switch_return().
 *
 * fork_trampoline(%rax:otd, %rbx:func, %r12:arg)
 */
ENTRY(fork_trampoline)
	movq	%rax,%rdi
	call	lwkt_switch_return
	movq	PCPU(curthread),%rax
	decl	TD_CRITCOUNT(%rax)

	/*
	 * cpu_set_fork_handler intercepts this function call to
	 * have this call a non-return function to stay in kernel mode.
	 *
	 * initproc has its own fork handler, start_init(), which DOES
	 * return.
	 *
	 * %rbx - chaining function (typically fork_return)
	 * %r12 -> %rdi (argument)
	 * frame-> %rsi (trap frame)
	 *
	 *   void (func:rbx)(arg:rdi, trapframe:rsi)
	 */
	movq	%rsp, %rsi		/* pass trapframe by reference */
	movq	%r12, %rdi		/* arg1 */
	call	*%rbx			/* function */

	/* cut from syscall */

	sti
	call	splz

	/*
	 * Return via doreti to handle ASTs.
	 *
	 * trapframe is at the top of the stack.
	 */
	MEXITCOUNT
	jmp	doreti

/*
 * To efficiently implement classification of trap and interrupt handlers
 * for profiling, there must be only trap handlers between the labels btrap
 * and bintr, and only interrupt handlers between the labels bintr and
 * eintr.  This is implemented (partly) by including files that contain
 * some of the handlers.  Before including the files, set up a normal asm
 * environment so that the included files doen't need to know that they are
 * included.
 */

#if 0 /* COMPAT_IA32 */
	.data
	.p2align 4
	.text
	SUPERALIGN_TEXT

#include <x86_64/ia32/ia32_exception.S>
#endif

	.data
	.p2align 4
	.text
	SUPERALIGN_TEXT
MCOUNT_LABEL(bintr)

#if 0 /* JG */
#include <x86_64/x86_64/apic_vector.S>
#endif

#ifdef DEV_ATPIC
	.data
	.p2align 4
	.text
	SUPERALIGN_TEXT

#include <x86_64/isa/atpic_vector.S>
#endif

	.text
MCOUNT_LABEL(eintr)
