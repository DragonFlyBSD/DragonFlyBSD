/*
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
 *
 * $FreeBSD: src/sys/amd64/include/asmacros.h,v 1.32 2006/10/28 06:04:29 bde Exp $
 */

#ifndef _CPU_ASMACROS_H_
#define _CPU_ASMACROS_H_

#include <sys/cdefs.h>

/* XXX too much duplication in various asm*.h's. */

/*
 * CNAME is used to manage the relationship between symbol names in C
 * and the equivalent assembly language names.  CNAME is given a name as
 * it would be used in a C program.  It expands to the equivalent assembly
 * language name.
 */
#define CNAME(csym)		csym

#define ALIGN_DATA	.p2align 3	/* 8 byte alignment, zero filled */
#ifdef GPROF
#define ALIGN_TEXT	.p2align 4,0x90	/* 16-byte alignment, nop filled */
#else
#define ALIGN_TEXT	.p2align 4,0x90	/* 16-byte alignment, nop filled */
#endif
#define SUPERALIGN_TEXT	.p2align 4,0x90	/* 16-byte alignment, nop filled */

#define GEN_ENTRY(name)		ALIGN_TEXT; .globl CNAME(name); \
				.type CNAME(name),@function; CNAME(name):
#define NON_GPROF_ENTRY(name)	GEN_ENTRY(name)
#define NON_GPROF_RET		.byte 0xc3	/* opcode for `ret' */

#define	END(name)		.size name, . - name

#ifdef GPROF
/*
 * __mcount is like [.]mcount except that doesn't require its caller to set
 * up a frame pointer.  It must be called before pushing anything onto the
 * stack.  gcc should eventually generate code to call __mcount in most
 * cases.  This would make -pg in combination with -fomit-frame-pointer
 * useful.  gcc has a configuration variable PROFILE_BEFORE_PROLOGUE to
 * allow profiling before setting up the frame pointer, but this is
 * inadequate for good handling of special cases, e.g., -fpic works best
 * with profiling after the prologue.
 *
 * [.]mexitcount is a new function to support non-statistical profiling if an
 * accurate clock is available.  For C sources, calls to it are generated
 * by the FreeBSD extension `-mprofiler-epilogue' to gcc.  It is best to
 * call [.]mexitcount at the end of a function like the MEXITCOUNT macro does,
 * but gcc currently generates calls to it at the start of the epilogue to
 * avoid problems with -fpic.
 *
 * [.]mcount and __mcount may clobber the call-used registers and %ef.
 * [.]mexitcount may clobber %ecx and %ef.
 *
 * Cross-jumping makes non-statistical profiling timing more complicated.
 * It is handled in many cases by calling [.]mexitcount before jumping.  It
 * is handled for conditional jumps using CROSSJUMP() and CROSSJUMP_LABEL().
 * It is handled for some fault-handling jumps by not sharing the exit
 * routine.
 *
 * ALTENTRY() must be before a corresponding ENTRY() so that it can jump to
 * the main entry point.  Note that alt entries are counted twice.  They
 * have to be counted as ordinary entries for gprof to get the call times
 * right for the ordinary entries.
 *
 * High local labels are used in macros to avoid clashes with local labels
 * in functions.
 *
 * Ordinary `ret' is used instead of a macro `RET' because there are a lot
 * of `ret's.  0xc3 is the opcode for `ret' (`#define ret ... ret' can't
 * be used because this file is sometimes preprocessed in traditional mode).
 * `ret' clobbers eflags but this doesn't matter.
 */
#define ALTENTRY(name)		GEN_ENTRY(name) ; MCOUNT ; MEXITCOUNT ; jmp 9f
#define	CROSSJUMP(jtrue, label, jfalse) \
	jfalse 8f; MEXITCOUNT; jmp __CONCAT(to,label); 8:
#define CROSSJUMPTARGET(label) \
	ALIGN_TEXT; __CONCAT(to,label): ; MCOUNT; jmp label
#define ENTRY(name)		GEN_ENTRY(name) ; 9: ; MCOUNT
#define FAKE_MCOUNT(caller)	pushq caller ; call __mcount ; popq %rcx
#define MCOUNT			call __mcount
#define MCOUNT_LABEL(name)	GEN_ENTRY(name) ; nop ; ALIGN_TEXT
#ifdef GUPROF
#define MEXITCOUNT		call .mexitcount
#define ret			MEXITCOUNT ; NON_GPROF_RET
#else
#define MEXITCOUNT
#endif

#else /* !GPROF */
/*
 * ALTENTRY() has to align because it is before a corresponding ENTRY().
 * ENTRY() has to align to because there may be no ALTENTRY() before it.
 * If there is a previous ALTENTRY() then the alignment code for ENTRY()
 * is empty.
 */
#define ALTENTRY(name)		GEN_ENTRY(name)
#define	CROSSJUMP(jtrue, label, jfalse)	jtrue label
#define	CROSSJUMPTARGET(label)
#define ENTRY(name)		GEN_ENTRY(name)
#define FAKE_MCOUNT(caller)
#define MCOUNT
#define MCOUNT_LABEL(name)
#define MEXITCOUNT
#endif /* GPROF */

#ifdef LOCORE
/*
 * Convenience macro for declaring interrupt entry points.
 */
#define	IDTVEC(name)	ALIGN_TEXT; .globl __CONCAT(X,name); \
			.type __CONCAT(X,name),@function; __CONCAT(X,name):

/*
 * stack frame macro support - supports mmu isolation, swapgs, and
 * stack frame pushing and popping.
 */

/*
 * Kernel pmap isolation to work-around the massive Intel mmu bug
 * that allows kernel memory to be sussed out due to speculative memory
 * reads and instruction execution creating timing differences that can
 * be detected by userland.  e.g. force speculative read, speculatively
 * execute a cmp/branch sequence, detect timing.  Iterate cmp $values
 * to suss-out content of speculatively read kernel memory.
 *
 * We do this by creating a trampoline area for all user->kernel and
 * kernel->user transitions.  The trampoline area allows us to limit
 * the reach the kernel map in the isolated version of the user pmap
 * to JUST the trampoline area (for all cpus), tss, and vector area.
 *
 * It is very important that these transitions not access any memory
 * outside of the trampoline page while the isolated user process pmap
 * is active in %cr3.
 *
 * The trampoline does not add much overhead when pmap isolation is
 * disabled, so we just run with it regardless.  Of course, when pmap
 * isolation is enabled, the %cr3 loads add 150-250ns to every system
 * call as well as (without PCID) smash the TLB.
 *
 * KMMUENTER -	Executed by the trampoline when a user->kernel transition
 *		is detected.  The stack pointer points into the pcpu
 *		trampoline space and is available for register save/restore.
 *		Other registers have not yet been saved.  %gs points at
 *		the kernel pcpu structure.
 *
 *		Caller has already determined that a transition is in
 *		progress and has already issued the swapgs.  hwtf indicates
 *		how much hardware has already pushed.
 *
 * KMMUEXIT  -	Executed when a kernel->user transition is made.  The stack
 *		pointer points into the pcpu trampoline space and we are
 *		almost ready to iretq.  %gs still points at the kernel pcpu
 *		structure.
 *
 *		Caller has already determined that a transition is in
 *		progress.  hwtf indicates how much hardware has already
 *		pushed.
 */
#define KMMUENTER_TFRIP							\
	subq	$TR_RIP, %rsp ;						\
	movq	%r10, TR_R10(%rsp) ;					\
	movq	%r11, TR_R11(%rsp) ;					\
	testq	$PCB_ISOMMU,PCPU(trampoline)+TR_PCB_FLAGS ;		\
	je	40f ;							\
	movq	PCPU(trampoline)+TR_PCB_CR3,%r10 ;			\
	movq	%r10,%cr3 ;						\
40:									\
	movq	%rsp, %r10 ;		/* trampoline rsp */		\
	movq	PCPU(trampoline)+TR_PCB_RSP,%rsp ; /* kstack rsp */	\
	movq	TR_SS(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_RSP(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_RFLAGS(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_CS(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_RIP(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_R11(%r10), %r11 ;					\
	movq	TR_R10(%r10), %r10					\

#define KMMUENTER_TFERR							\
	subq	$TR_ERR, %rsp ;						\
	movq	%r10, TR_R10(%rsp) ;					\
	movq	%r11, TR_R11(%rsp) ;					\
	testq	$PCB_ISOMMU,PCPU(trampoline)+TR_PCB_FLAGS ;		\
	je	40f ;							\
	movq	PCPU(trampoline)+TR_PCB_CR3,%r10 ;			\
	movq	%r10,%cr3 ;						\
40:									\
	movq	%rsp, %r10 ;		/* trampoline rsp */		\
	movq	PCPU(trampoline)+TR_PCB_RSP,%rsp ; /* kstack rsp */	\
	movq	TR_SS(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_RSP(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_RFLAGS(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_CS(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_RIP(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_ERR(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_R11(%r10), %r11 ;					\
	movq	TR_R10(%r10), %r10					\

#define KMMUENTER_TFERR_SAVECR2						\
	subq	$TR_ERR, %rsp ;						\
	movq	%r10, TR_R10(%rsp) ;					\
	movq	%r11, TR_R11(%rsp) ;					\
	movq	%cr2, %r10 ;						\
	movq	%r10, PCPU(trampoline)+TR_CR2 ;				\
	testq	$PCB_ISOMMU,PCPU(trampoline)+TR_PCB_FLAGS ;		\
	je	40f ;							\
	movq	PCPU(trampoline)+TR_PCB_CR3,%r10 ;			\
	movq	%r10,%cr3 ;						\
40:									\
	movq	%rsp, %r10 ;		/* trampoline rsp */		\
	movq	PCPU(trampoline)+TR_PCB_RSP,%rsp ; /* kstack rsp */	\
	movq	TR_SS(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_RSP(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_RFLAGS(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_CS(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_RIP(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_ERR(%r10), %r11 ;					\
	pushq	%r11 ;							\
	movq	TR_R11(%r10), %r11 ;					\
	movq	TR_R10(%r10), %r10					\

/*
 * Set %cr3 if necessary on syscall entry.  No registers may be
 * disturbed.
 */
#define KMMUENTER_SYSCALL						\
	testq	$PCB_ISOMMU,PCPU(trampoline)+TR_PCB_FLAGS ;		\
	je	40f ;							\
	pushq	%r10 ;							\
	movq	PCPU(trampoline)+TR_PCB_CR3,%r10 ;			\
	movq	%r10,%cr3 ;						\
	popq	%r10 ;							\
40:									\

/*
 * We are positioned at the base of the trapframe.  Advance the trapframe
 * and handle MMU isolation.  MMU isolation requires us to copy the
 * hardware frame to the trampoline area before setting %cr3 to the
 * isolated map.  We then set the %rsp for iretq to TR_RIP in the
 * trampoline area (after restoring the register we saved in TR_ERR).
 */
#define KMMUEXIT							\
	addq	$TF_RIP,%rsp ;						\
	testq	$PCB_ISOMMU,PCPU(trampoline)+TR_PCB_FLAGS ;		\
	je	40f ;							\
	movq	%r11, PCPU(trampoline)+TR_ERR ;	/* save in TR_ERR */	\
	popq	%r11 ;				/* copy %rip */		\
	movq	%r11, PCPU(trampoline)+TR_RIP ;				\
	popq	%r11 ;				/* copy %cs */		\
	movq	%r11, PCPU(trampoline)+TR_CS ;				\
	popq	%r11 ;				/* copy %rflags */	\
	movq	%r11, PCPU(trampoline)+TR_RFLAGS ;			\
	popq	%r11 ;				/* copy %rsp */		\
	movq	%r11, PCPU(trampoline)+TR_RSP ;				\
	popq	%r11 ;				/* copy %ss */		\
	movq	%r11, PCPU(trampoline)+TR_SS ;				\
	movq	%gs:0,%r11 ;						\
	addq	$GD_TRAMPOLINE+TR_ERR,%r11 ;				\
	movq	%r11,%rsp ;						\
	movq	PCPU(trampoline)+TR_PCB_CR3_ISO,%r11 ;			\
	movq	%r11,%cr3 ;						\
	popq	%r11 ;		/* positioned at TR_RIP after this */	\
40:									\

/*
 * Warning: user stack pointer already loaded into %rsp at this
 * point.  We still have the kernel %gs.
 */
#define KMMUEXIT_SYSCALL						\
	testq	$PCB_ISOMMU,PCPU(trampoline)+TR_PCB_FLAGS ;		\
	je	40f ;							\
	movq	%r10, PCPU(trampoline)+TR_R10 ;				\
	movq	PCPU(trampoline)+TR_PCB_CR3_ISO,%r10 ;			\
	movq	%r10,%cr3 ;						\
	movq	PCPU(trampoline)+TR_R10, %r10 ;				\
40:									\

/*
 * Macros to create and destroy a trap frame.  rsp has already been shifted
 * to the base of the trapframe in the thread structure.
 */
#define PUSH_FRAME_REGS							\
	movq	%rdi,TF_RDI(%rsp) ;					\
	movq	%rsi,TF_RSI(%rsp) ;					\
	movq	%rdx,TF_RDX(%rsp) ;					\
	movq	%rcx,TF_RCX(%rsp) ;					\
	movq	%r8,TF_R8(%rsp) ;					\
	movq	%r9,TF_R9(%rsp) ;					\
	movq	%rax,TF_RAX(%rsp) ;					\
	movq	%rbx,TF_RBX(%rsp) ;					\
	movq	%rbp,TF_RBP(%rsp) ;					\
	movq	%r10,TF_R10(%rsp) ;					\
	movq	%r11,TF_R11(%rsp) ;					\
	movq	%r12,TF_R12(%rsp) ;					\
	movq	%r13,TF_R13(%rsp) ;					\
	movq	%r14,TF_R14(%rsp) ;					\
	movq	%r15,TF_R15(%rsp)

/*
 * PUSH_FRAME is the first thing executed upon interrupt entry.  We are
 * responsible for swapgs execution and the KMMUENTER dispatch.
 */
#define PUSH_FRAME_TFRIP						\
	testb	$SEL_RPL_MASK,TF_CS-TF_RIP(%rsp) ; /* from userland? */	\
	jz	1f ;							\
	swapgs ;		/* from userland */			\
	KMMUENTER_TFRIP ;	/* from userland */			\
1:									\
	subq	$TF_RIP,%rsp ;						\
	PUSH_FRAME_REGS 						\

#define PUSH_FRAME_TFERR						\
	testb	$SEL_RPL_MASK,TF_CS-TF_ERR(%rsp) ; /* from userland? */	\
	jz	1f ;							\
	swapgs ;		/* from userland */			\
	KMMUENTER_TFERR ;	/* from userland */			\
1:									\
	subq	$TF_ERR,%rsp ;						\
	PUSH_FRAME_REGS 						\

#define PUSH_FRAME_TFERR_SAVECR2					\
	testb	$SEL_RPL_MASK,TF_CS-TF_ERR(%rsp) ;			\
	jz	1f ;							\
	swapgs ;		/* from userland */			\
	KMMUENTER_TFERR_SAVECR2 ;/* from userland */			\
	subq	$TF_ERR,%rsp ;						\
	PUSH_FRAME_REGS ;						\
	movq	PCPU(trampoline)+TR_CR2, %r10 ;				\
	jmp 2f ;							\
1:									\
	subq	$TF_ERR,%rsp ;						\
	PUSH_FRAME_REGS ;						\
	movq	%cr2, %r10 ;						\
2:									\
	movq	%r10, TF_ADDR(%rsp)

/*
 * Called when the iretq in doreti_iret faults.  XXX
 */
#define PUSH_FRAME_NOSWAP						\
	KMMUENTER_TFRIP ;						\
	PUSH_FRAME_REGS							\

/*
 * POP_FRAME is issued just prior to the iretq, or just prior to a
 * jmp doreti_iret.  These must be passed in to the macro.
 */
#define POP_FRAME(lastinsn)						\
	movq	TF_RDI(%rsp),%rdi ;					\
	movq	TF_RSI(%rsp),%rsi ;					\
	movq	TF_RDX(%rsp),%rdx ;					\
	movq	TF_RCX(%rsp),%rcx ;					\
	movq	TF_R8(%rsp),%r8 ;					\
	movq	TF_R9(%rsp),%r9 ;					\
	movq	TF_RAX(%rsp),%rax ;					\
	movq	TF_RBX(%rsp),%rbx ;					\
	movq	TF_RBP(%rsp),%rbp ;					\
	movq	TF_R10(%rsp),%r10 ;					\
	movq	TF_R11(%rsp),%r11 ;					\
	movq	TF_R12(%rsp),%r12 ;					\
	movq	TF_R13(%rsp),%r13 ;					\
	movq	TF_R14(%rsp),%r14 ;					\
	movq	TF_R15(%rsp),%r15 ;					\
	cli ;								\
	testb	$SEL_RPL_MASK,TF_CS(%rsp) ; /* return to user? */	\
	jz	1f ;							\
	KMMUEXIT ;		/* return to user */			\
	swapgs ;		/* return to user */			\
	jmp	2f ;							\
1:									\
	addq	$TF_RIP,%rsp ;	/* setup for iretq */			\
2:									\
	lastinsn

/*
 * Access per-CPU data.
 */
#define	PCPU(member)		%gs:gd_ ## member
#define PCPU_E8(member,idx)	%gs:gd_ ## member(,idx,8)
#define	PCPU_ADDR(member, reg)					\
	movq %gs:PC_PRVSPACE, reg ;				\
	addq $PC_ ## member, reg

#endif /* LOCORE */

#endif /* !_CPU_ASMACROS_H_ */
