/*
 * Copyright (c) 2003,2004,2008 The DragonFly Project.  All rights reserved.
 * Copyright (c) 2008 Jordan Gordeev.
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
 * 
 * Copyright (c) 1990 The Regents of the University of California.
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
 * $FreeBSD: src/sys/i386/i386/swtch.s,v 1.89.2.10 2003/01/23 03:36:24 ps Exp $
 */

//#include "use_npx.h"

#include <sys/rtprio.h>

#include <machine/asmacros.h>
#include <machine/segments.h>

#include <machine/pmap.h>
#if JG
#include <machine_base/apic/apicreg.h>
#endif
#include <machine/lock.h>

#include "assym.s"

#define MPLOCKED        lock ;

	.data

	.globl	panic
	.globl	lwkt_switch_return

#if defined(SWTCH_OPTIM_STATS)
	.globl	swtch_optim_stats, tlb_flush_count
swtch_optim_stats:	.long	0		/* number of _swtch_optims */
tlb_flush_count:	.long	0
#endif

	.text


/*
 * cpu_heavy_switch(struct thread *next_thread)
 *
 *	Switch from the current thread to a new thread.  This entry
 *	is normally called via the thread->td_switch function, and will
 *	only be called when the current thread is a heavy weight process.
 *
 *	Some instructions have been reordered to reduce pipeline stalls.
 *
 *	YYY disable interrupts once giant is removed.
 */
ENTRY(cpu_heavy_switch)
	/*
	 * Save RIP, RSP and callee-saved registers (RBX, RBP, R12-R15).
	 */
	movq	PCPU(curthread),%rcx
	/* On top of the stack is the return adress. */
	movq	(%rsp),%rax			/* (reorder optimization) */
	movq	TD_PCB(%rcx),%rdx		/* RDX = PCB */
	movq	%rax,PCB_RIP(%rdx)		/* return PC may be modified */
	movq	%rbx,PCB_RBX(%rdx)
	movq	%rsp,PCB_RSP(%rdx)
	movq	%rbp,PCB_RBP(%rdx)
	movq	%r12,PCB_R12(%rdx)
	movq	%r13,PCB_R13(%rdx)
	movq	%r14,PCB_R14(%rdx)
	movq	%r15,PCB_R15(%rdx)

	/*
	 * Clear the cpu bit in the pmap active mask.  The restore
	 * function will set the bit in the pmap active mask.
	 *
	 * Special case: when switching between threads sharing the
	 * same vmspace if we avoid clearing the bit we do not have
	 * to reload %cr3 (if we clear the bit we could race page
	 * table ops done by other threads and would have to reload
	 * %cr3, because those ops will not know to IPI us).
	 */
	movq	%rcx,%rbx			/* RBX = oldthread */
	movq	TD_LWP(%rcx),%rcx		/* RCX = oldlwp	*/
	movq	TD_LWP(%rdi),%r13		/* R13 = newlwp */
	movq	LWP_VMSPACE(%rcx), %rcx		/* RCX = oldvmspace */
	testq	%r13,%r13			/* might not be a heavy */
	jz	1f
	cmpq	LWP_VMSPACE(%r13),%rcx		/* same vmspace? */
	je	2f
1:
	movslq	PCPU(cpuid), %rax
	MPLOCKED btrq	%rax, VM_PMAP+PM_ACTIVE(%rcx)
2:

	/*
	 * Push the LWKT switch restore function, which resumes a heavy
	 * weight process.  Note that the LWKT switcher is based on
	 * TD_SP, while the heavy weight process switcher is based on
	 * PCB_RSP.  TD_SP is usually two ints pushed relative to
	 * PCB_RSP.  We push the flags for later restore by cpu_heavy_restore.
	 */
	pushfq
	cli
	movq	$cpu_heavy_restore, %rax
	pushq	%rax
	movq	%rsp,TD_SP(%rbx)

	/*
	 * Save debug regs if necessary
	 */
	movq    PCB_FLAGS(%rdx),%rax
	andq    $PCB_DBREGS,%rax
	jz      1f                              /* no, skip over */
	movq    %dr7,%rax                       /* yes, do the save */
	movq    %rax,PCB_DR7(%rdx)
	/* JG correct value? */
	andq    $0x0000fc00, %rax               /* disable all watchpoints */
	movq    %rax,%dr7
	movq    %dr6,%rax
	movq    %rax,PCB_DR6(%rdx)
	movq    %dr3,%rax
	movq    %rax,PCB_DR3(%rdx)
	movq    %dr2,%rax
	movq    %rax,PCB_DR2(%rdx)
	movq    %dr1,%rax
	movq    %rax,PCB_DR1(%rdx)
	movq    %dr0,%rax
	movq    %rax,PCB_DR0(%rdx)
1:
 
#if 1
	/*
	 * Save the FP state if we have used the FP.  Note that calling
	 * npxsave will NULL out PCPU(npxthread).
	 */
	cmpq	%rbx,PCPU(npxthread)
	jne	1f
	movq	%rdi,%r12		/* save %rdi. %r12 is callee-saved */
	movq	TD_SAVEFPU(%rbx),%rdi
	call	npxsave			/* do it in a big C function */
	movq	%r12,%rdi		/* restore %rdi */
1:
#endif

	/*
	 * Switch to the next thread, which was passed as an argument
	 * to cpu_heavy_switch().  The argument is in %rdi.
	 * Set the current thread, load the stack pointer,
	 * and 'ret' into the switch-restore function.
	 *
	 * The switch restore function expects the new thread to be in %rax
	 * and the old one to be in %rbx.
	 *
	 * There is a one-instruction window where curthread is the new
	 * thread but %rsp still points to the old thread's stack, but
	 * we are protected by a critical section so it is ok.
	 */
	movq	%rdi,%rax		/* RAX = newtd, RBX = oldtd */
	movq	%rax,PCPU(curthread)
	movq	TD_SP(%rax),%rsp
	ret

/*
 *  cpu_exit_switch(struct thread *next)
 *
 *	The switch function is changed to this when a thread is going away
 *	for good.  We have to ensure that the MMU state is not cached, and
 *	we don't bother saving the existing thread state before switching.
 *
 *	At this point we are in a critical section and this cpu owns the
 *	thread's token, which serves as an interlock until the switchout is
 *	complete.
 */
ENTRY(cpu_exit_switch)
	/*
	 * Get us out of the vmspace
	 */
	movq	KPML4phys,%rcx
	movq	%cr3,%rax
#if 1
	cmpq	%rcx,%rax
	je	1f
#endif
	/* JG no increment of statistics counters? see cpu_heavy_restore */
	movq	%rcx,%cr3
1:
	movq	PCPU(curthread),%rbx

	/*
	 * If this is a process/lwp, deactivate the pmap after we've
	 * switched it out.
	 */
	movq	TD_LWP(%rbx),%rcx
	testq	%rcx,%rcx
	jz	2f
	movslq	PCPU(cpuid), %rax
	movq	LWP_VMSPACE(%rcx), %rcx		/* RCX = vmspace */
	MPLOCKED btrq	%rax, VM_PMAP+PM_ACTIVE(%rcx)
2:
	/*
	 * Switch to the next thread.  RET into the restore function, which
	 * expects the new thread in RAX and the old in RBX.
	 *
	 * There is a one-instruction window where curthread is the new
	 * thread but %rsp still points to the old thread's stack, but
	 * we are protected by a critical section so it is ok.
	 */
	cli
	movq	%rdi,%rax
	movq	%rax,PCPU(curthread)
	movq	TD_SP(%rax),%rsp
	ret

/*
 * cpu_heavy_restore()	(current thread in %rax on entry, old thread in %rbx)
 *
 *	Restore the thread after an LWKT switch.  This entry is normally
 *	called via the LWKT switch restore function, which was pulled 
 *	off the thread stack and jumped to.
 *
 *	This entry is only called if the thread was previously saved
 *	using cpu_heavy_switch() (the heavy weight process thread switcher),
 *	or when a new process is initially scheduled.
 *
 *	NOTE: The lwp may be in any state, not necessarily LSRUN, because
 *	a preemption switch may interrupt the process and then return via 
 *	cpu_heavy_restore.
 *
 *	YYY theoretically we do not have to restore everything here, a lot
 *	of this junk can wait until we return to usermode.  But for now
 *	we restore everything.
 *
 *	YYY the PCB crap is really crap, it makes startup a bitch because
 *	we can't switch away.
 *
 *	YYY note: spl check is done in mi_switch when it splx()'s.
 */

ENTRY(cpu_heavy_restore)
	movq	TD_PCB(%rax),%rdx		/* RDX = PCB */
	movq	%rdx, PCPU(common_tss) + TSS_RSP0
	popfq

#if defined(SWTCH_OPTIM_STATS)
	incl	_swtch_optim_stats
#endif
	/*
	 * Tell the pmap that our cpu is using the VMSPACE now.  We cannot
	 * safely test/reload %cr3 until after we have set the bit in the
	 * pmap.
	 *
	 * We must do an interlocked test of the CPULOCK_EXCL at the same
	 * time.  If found to be set we will have to wait for it to clear
	 * and then do a forced reload of %cr3 (even if the value matches).
	 *
	 * XXX When switching between two LWPs sharing the same vmspace
	 *     the cpu_heavy_switch() code currently avoids clearing the
	 *     cpu bit in PM_ACTIVE.  So if the bit is already set we can
	 *     avoid checking for the interlock via CPULOCK_EXCL.  We currently
	 *     do not perform this optimization.
	 */
	movq	TD_LWP(%rax),%rcx
	movq	LWP_VMSPACE(%rcx),%rcx		/* RCX = vmspace */
	movq	PCPU(cpumask),%rsi		/* new contents */
	MPLOCKED orq %rsi, VM_PMAP+PM_ACTIVE(%rcx)
	movl	VM_PMAP+PM_ACTIVE_LOCK(%rcx),%esi
	testl	$CPULOCK_EXCL,%esi
	jz	1f

	movq	%rax,%r12		/* save newthread ptr */
	movq	%rcx,%rdi		/* (found to be set) */
	call	pmap_interlock_wait	/* pmap_interlock_wait(%rdi:vm) */
	movq	%r12,%rax

	/*
	 * Need unconditional load cr3
	 */
	movq	TD_PCB(%rax),%rdx	/* RDX = PCB */
	movq	PCB_CR3(%rdx),%rcx	/* RCX = desired CR3 */
	jmp	2f			/* unconditional reload */
1:
	/*
	 * Restore the MMU address space.  If it is the same as the last
	 * thread we don't have to invalidate the tlb (i.e. reload cr3).
	 * YYY which naturally also means that the PM_ACTIVE bit had better
	 * already have been set before we set it above, check? YYY
	 */
	movq	TD_PCB(%rax),%rdx		/* RDX = PCB */
	movq	%cr3,%rsi			/* RSI = current CR3 */
	movq	PCB_CR3(%rdx),%rcx		/* RCX = desired CR3 */
	cmpq	%rsi,%rcx
	je	4f
2:
#if defined(SWTCH_OPTIM_STATS)
	decl	_swtch_optim_stats
	incl	_tlb_flush_count
#endif
	movq	%rcx,%cr3
4:

	/*
	 * NOTE: %rbx is the previous thread and %rax is the new thread.
	 *	 %rbx is retained throughout so we can return it.
	 *
	 *	 lwkt_switch[_return] is responsible for handling TDF_RUNNING.
	 */

	/*
	 * Deal with the PCB extension, restore the private tss
	 */
	movq	PCB_EXT(%rdx),%rdi	/* check for a PCB extension */
	movq	$1,%rcx			/* maybe mark use of a private tss */
	testq	%rdi,%rdi
#if JG
	jnz	2f
#endif

	/*
	 * Going back to the common_tss.  We may need to update TSS_RSP0
	 * which sets the top of the supervisor stack when entering from
	 * usermode.  The PCB is at the top of the stack but we need another
	 * 16 bytes to take vm86 into account.
	 */
	movq	%rdx,%rcx
	/*leaq	-TF_SIZE(%rdx),%rcx*/
	movq	%rcx, PCPU(common_tss) + TSS_RSP0

#if JG
	cmpl	$0,PCPU(private_tss)	/* don't have to reload if      */
	je	3f			/* already using the common TSS */

	/* JG? */
	subq	%rcx,%rcx		/* unmark use of private tss */

	/*
	 * Get the address of the common TSS descriptor for the ltr.
	 * There is no way to get the address of a segment-accessed variable
	 * so we store a self-referential pointer at the base of the per-cpu
	 * data area and add the appropriate offset.
	 */
	/* JG movl? */
	movq	$gd_common_tssd, %rdi
	/* JG name for "%gs:0"? */
	addq	%gs:0, %rdi

	/*
	 * Move the correct TSS descriptor into the GDT slot, then reload
	 * ltr.
	 */
2:
	/* JG */
	movl	%rcx,PCPU(private_tss)		/* mark/unmark private tss */
	movq	PCPU(tss_gdt), %rbx		/* entry in GDT */
	movq	0(%rdi), %rax
	movq	%rax, 0(%rbx)
	movl	$GPROC0_SEL*8, %esi		/* GSEL(entry, SEL_KPL) */
	ltr	%si
#endif

3:
	/*
	 * Restore the user %gs and %fs
	 */
	movq	PCB_FSBASE(%rdx),%r9
	cmpq	PCPU(user_fs),%r9
	je	4f
	movq	%rdx,%r10
	movq	%r9,PCPU(user_fs)
	movl	$MSR_FSBASE,%ecx
	movl	PCB_FSBASE(%r10),%eax
	movl	PCB_FSBASE+4(%r10),%edx
	wrmsr
	movq	%r10,%rdx
4:
	movq	PCB_GSBASE(%rdx),%r9
	cmpq	PCPU(user_gs),%r9
	je	5f
	movq	%rdx,%r10
	movq	%r9,PCPU(user_gs)
	movl	$MSR_KGSBASE,%ecx	/* later swapgs moves it to GSBASE */
	movl	PCB_GSBASE(%r10),%eax
	movl	PCB_GSBASE+4(%r10),%edx
	wrmsr
	movq	%r10,%rdx
5:

	/*
	 * Restore general registers.  %rbx is restored later.
	 */
	movq	PCB_RSP(%rdx), %rsp
	movq	PCB_RBP(%rdx), %rbp
	movq	PCB_R12(%rdx), %r12
	movq	PCB_R13(%rdx), %r13
	movq	PCB_R14(%rdx), %r14
	movq	PCB_R15(%rdx), %r15
	movq	PCB_RIP(%rdx), %rax
	movq	%rax, (%rsp)
	movw	$KDSEL,%ax
	movw	%ax,%es

#if JG
	/*
	 * Restore the user LDT if we have one
	 */
	cmpl	$0, PCB_USERLDT(%edx)
	jnz	1f
	movl	_default_ldt,%eax
	cmpl	PCPU(currentldt),%eax
	je	2f
	lldt	_default_ldt
	movl	%eax,PCPU(currentldt)
	jmp	2f
1:	pushl	%edx
	call	set_user_ldt
	popl	%edx
2:
#endif
#if JG
	/*
	 * Restore the user TLS if we have one
	 */
	pushl	%edx
	call	set_user_TLS
	popl	%edx
#endif

	/*
	 * Restore the DEBUG register state if necessary.
	 */
	movq    PCB_FLAGS(%rdx),%rax
	andq    $PCB_DBREGS,%rax
	jz      1f                              /* no, skip over */
	movq    PCB_DR6(%rdx),%rax              /* yes, do the restore */
	movq    %rax,%dr6
	movq    PCB_DR3(%rdx),%rax
	movq    %rax,%dr3
	movq    PCB_DR2(%rdx),%rax
	movq    %rax,%dr2
	movq    PCB_DR1(%rdx),%rax
	movq    %rax,%dr1
	movq    PCB_DR0(%rdx),%rax
	movq    %rax,%dr0
	movq	%dr7,%rax                /* load dr7 so as not to disturb */
	/* JG correct value? */
	andq    $0x0000fc00,%rax         /*   reserved bits               */
	/* JG we've got more registers on x86_64 */
	movq    PCB_DR7(%rdx),%rcx
	/* JG correct value? */
	andq	$~0x0000fc00,%rcx
	orq     %rcx,%rax
	movq    %rax,%dr7

	/*
	 * Clear the QUICKRET flag when restoring a user process context
	 * so we don't try to do a quick syscall return.
	 */
1:
	andl	$~RQF_QUICKRET,PCPU(reqflags)
	movq	%rbx,%rax
	movq	PCB_RBX(%rdx),%rbx
	ret

/*
 * savectx(struct pcb *pcb)
 *
 * Update pcb, saving current processor state.
 */
ENTRY(savectx)
	/* fetch PCB */
	/* JG use %rdi instead of %rcx everywhere? */
	movq	%rdi,%rcx

	/* caller's return address - child won't execute this routine */
	movq	(%rsp),%rax
	movq	%rax,PCB_RIP(%rcx)

	movq	%cr3,%rax
	movq	%rax,PCB_CR3(%rcx)

	movq	%rbx,PCB_RBX(%rcx)
	movq	%rsp,PCB_RSP(%rcx)
	movq	%rbp,PCB_RBP(%rcx)
	movq	%r12,PCB_R12(%rcx)
	movq	%r13,PCB_R13(%rcx)
	movq	%r14,PCB_R14(%rcx)
	movq	%r15,PCB_R15(%rcx)

#if 1
	/*
	 * If npxthread == NULL, then the npx h/w state is irrelevant and the
	 * state had better already be in the pcb.  This is true for forks
	 * but not for dumps (the old book-keeping with FP flags in the pcb
	 * always lost for dumps because the dump pcb has 0 flags).
	 *
	 * If npxthread != NULL, then we have to save the npx h/w state to
	 * npxthread's pcb and copy it to the requested pcb, or save to the
	 * requested pcb and reload.  Copying is easier because we would
	 * have to handle h/w bugs for reloading.  We used to lose the
	 * parent's npx state for forks by forgetting to reload.
	 */
	movq	PCPU(npxthread),%rax
	testq	%rax,%rax
	jz	1f

	pushq	%rcx			/* target pcb */
	movq	TD_SAVEFPU(%rax),%rax	/* originating savefpu area */
	pushq	%rax

	movq	%rax,%rdi
	call	npxsave

	popq	%rax
	popq	%rcx

	movq	$PCB_SAVEFPU_SIZE,%rdx
	leaq    PCB_SAVEFPU(%rcx),%rcx
	movq	%rcx,%rsi
	movq	%rax,%rdi
	call	bcopy
#endif

1:
	ret

/*
 * cpu_idle_restore()	(current thread in %rax on entry) (one-time execution)
 *
 *	Don't bother setting up any regs other than %rbp so backtraces
 *	don't die.  This restore function is used to bootstrap into the
 *	cpu_idle() LWKT only, after that cpu_lwkt_*() will be used for
 *	switching.
 *
 *	Clear TDF_RUNNING in old thread only after we've cleaned up %cr3.
 *	This only occurs during system boot so no special handling is
 *	required for migration.
 *
 *	If we are an AP we have to call ap_init() before jumping to
 *	cpu_idle().  ap_init() will synchronize with the BP and finish
 *	setting up various ncpu-dependant globaldata fields.  This may
 *	happen on UP as well as SMP if we happen to be simulating multiple
 *	cpus.
 */
ENTRY(cpu_idle_restore)
	/* cli */
	movq	KPML4phys,%rcx
	/* JG xor? */
	movq	$0,%rbp
	/* JG push RBP? */
	pushq	$0
	movq	%rcx,%cr3
	cmpl	$0,PCPU(cpuid)
	je	1f
	andl	$~TDF_RUNNING,TD_FLAGS(%rbx)
	orl	$TDF_RUNNING,TD_FLAGS(%rax)	/* manual, no switch_return */
	call	ap_init
	/*
	 * ap_init can decide to enable interrupts early, but otherwise, or if
	 * we are UP, do it here.
	 */
	sti
	jmp	cpu_idle

	/*
	 * cpu 0's idle thread entry for the first time must use normal
	 * lwkt_switch_return() semantics or a pending cpu migration on
	 * thread0 will deadlock.
	 */
1:
	sti
	pushq	%rax
	movq	%rbx,%rdi
	call	lwkt_switch_return
	popq	%rax
	jmp	cpu_idle

/*
 * cpu_kthread_restore() (current thread is %rax on entry, previous is %rbx)
 *			 (one-time execution)
 *
 *	Don't bother setting up any regs other then %rbp so backtraces
 *	don't die.  This restore function is used to bootstrap into an
 *	LWKT based kernel thread only.  cpu_lwkt_switch() will be used
 *	after this.
 *
 *	Because this switch target does not 'return' to lwkt_switch()
 *	we have to call lwkt_switch_return(otd) to clean up otd.
 *	otd is in %ebx.
 *
 *	Since all of our context is on the stack we are reentrant and
 *	we can release our critical section and enable interrupts early.
 */
ENTRY(cpu_kthread_restore)
	sti
	movq	KPML4phys,%rcx
	movq	TD_PCB(%rax),%r13
	xorq	%rbp,%rbp
	movq	%rcx,%cr3

	/*
	 * rax and rbx come from the switchout code.  Call
	 * lwkt_switch_return(otd).
	 *
	 * NOTE: unlike i386, %rsi and %rdi are not call-saved regs.
	 */
	pushq	%rax
	movq	%rbx,%rdi
	call	lwkt_switch_return
	popq	%rax
	decl	TD_CRITCOUNT(%rax)
	movq	PCB_R12(%r13),%rdi	/* argument to RBX function */
	movq	PCB_RBX(%r13),%rax	/* thread function */
	/* note: top of stack return address inherited by function */
	jmp	*%rax

/*
 * cpu_lwkt_switch(struct thread *)
 *
 *	Standard LWKT switching function.  Only non-scratch registers are
 *	saved and we don't bother with the MMU state or anything else.
 *
 *	This function is always called while in a critical section.
 *
 *	There is a one-instruction window where curthread is the new
 *	thread but %rsp still points to the old thread's stack, but
 *	we are protected by a critical section so it is ok.
 */
ENTRY(cpu_lwkt_switch)
	pushq	%rbp	/* JG note: GDB hacked to locate ebp rel to td_sp */
	pushq	%rbx
	movq	PCPU(curthread),%rbx	/* becomes old thread in restore */
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	pushfq
	cli

#if 1
	/*
	 * Save the FP state if we have used the FP.  Note that calling
	 * npxsave will NULL out PCPU(npxthread).
	 *
	 * We have to deal with the FP state for LWKT threads in case they
	 * happen to get preempted or block while doing an optimized
	 * bzero/bcopy/memcpy.
	 */
	cmpq	%rbx,PCPU(npxthread)
	jne	1f
	movq	%rdi,%r12		/* save %rdi. %r12 is callee-saved */
	movq	TD_SAVEFPU(%rbx),%rdi
	call	npxsave			/* do it in a big C function */
	movq	%r12,%rdi		/* restore %rdi */
1:
#endif

	movq	%rdi,%rax		/* switch to this thread */
	pushq	$cpu_lwkt_restore
	movq	%rsp,TD_SP(%rbx)
	/*
	 * %rax contains new thread, %rbx contains old thread.
	 */
	movq	%rax,PCPU(curthread)
	movq	TD_SP(%rax),%rsp
	ret

/*
 * cpu_lwkt_restore()	(current thread in %rax on entry)
 *
 *	Standard LWKT restore function.  This function is always called
 *	while in a critical section.
 *	
 *	Warning: due to preemption the restore function can be used to 
 *	'return' to the original thread.  Interrupt disablement must be
 *	protected through the switch so we cannot run splz here.
 *
 *	YYY we theoretically do not need to load KPML4phys into cr3, but if
 *	so we need a way to detect when the PTD we are using is being 
 *	deleted due to a process exiting.
 */
ENTRY(cpu_lwkt_restore)
	movq	KPML4phys,%rcx	/* YYY borrow but beware desched/cpuchg/exit */
	movq	%cr3,%rdx
#if 1
	cmpq	%rcx,%rdx
	je	1f
#endif
	movq	%rcx,%cr3
1:
	/*
	 * Safety, clear RSP0 in the tss so it isn't pointing at the
	 * previous thread's kstack (if a heavy weight user thread).
	 * RSP0 should only be used in ring 3 transitions and kernel
	 * threads run in ring 0 so there should be none.
	 */
	xorq	%rdx,%rdx
	movq	%rdx, PCPU(common_tss) + TSS_RSP0

	/*
	 * NOTE: %rbx is the previous thread and %rax is the new thread.
	 *	 %rbx is retained throughout so we can return it.
	 *
	 *	 lwkt_switch[_return] is responsible for handling TDF_RUNNING.
	 */
	movq	%rbx,%rax
	popfq
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp
	ret
