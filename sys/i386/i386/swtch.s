/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 * LWKT threads Copyright (c) 2003 Matthew Dillon
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
 * $DragonFly: src/sys/i386/i386/Attic/swtch.s,v 1.8 2003/06/21 07:54:56 dillon Exp $
 */

#include "npx.h"
#include "opt_user_ldt.h"

#include <sys/rtprio.h>

#include <machine/asmacros.h>
#include <machine/ipl.h>

#ifdef SMP
#include <machine/pmap.h>
#include <machine/smptests.h>		/** GRAB_LOPRIO */
#include <machine/apic.h>
#include <machine/lock.h>
#endif /* SMP */

#include "assym.s"

	.data

	.globl	_panic

#if defined(SWTCH_OPTIM_STATS)
	.globl	_swtch_optim_stats, _tlb_flush_count
_swtch_optim_stats:	.long	0		/* number of _swtch_optims */
_tlb_flush_count:	.long	0
#endif

	.text


/*
 * cpu_heavy_switch(next_thread)
 *
 *	Switch from the current thread to a new thread.  This entry
 *	is normally called via the thread->td_switch function, and will
 *	only be called when the current thread is a heavy weight process.
 *
 *	YYY disable interrupts once giant is removed.
 */
ENTRY(cpu_heavy_switch)
	movl	_curthread,%ecx
	movl	_cpl,%edx			/* YYY temporary */
	movl	%edx,TD_MACH+MTD_CPL(%ecx)	/* YYY temporary */
	movl	TD_PROC(%ecx),%ecx

	cli
#ifdef SMP
	movb	P_ONCPU(%ecx), %al		/* save "last" cpu */
	movb	%al, P_LASTCPU(%ecx)
	movb	$0xff, P_ONCPU(%ecx)		/* "leave" the cpu */
#endif /* SMP */
	movl	P_VMSPACE(%ecx), %edx
#ifdef SMP
	movl	_cpuid, %eax
#else
	xorl	%eax, %eax
#endif /* SMP */
	btrl	%eax, VM_PMAP+PM_ACTIVE(%edx)

	/*
	 * Save general regs
	 */
	movl	P_THREAD(%ecx),%edx
	movl	TD_PCB(%edx),%edx
	movl	(%esp),%eax			/* Hardware registers */
	movl	%eax,PCB_EIP(%edx)
	movl	%ebx,PCB_EBX(%edx)
	movl	%esp,PCB_ESP(%edx)
	movl	%ebp,PCB_EBP(%edx)
	movl	%esi,PCB_ESI(%edx)
	movl	%edi,PCB_EDI(%edx)
	movl	%gs,PCB_GS(%edx)

	/*
	 * Push the LWKT switch restore function, which resumes a heavy
	 * weight process.  Note that the LWKT switcher is based on
	 * TD_SP, while the heavy weight process switcher is based on
	 * PCB_ESP.  TD_SP is usually one pointer pushed relative to
	 * PCB_ESP.
	 */
	movl	P_THREAD(%ecx),%eax
	pushl	$cpu_heavy_restore
	movl	%esp,TD_SP(%eax)

	/*
	 * Save debug regs if necessary
	 */
	movb    PCB_FLAGS(%edx),%al
	andb    $PCB_DBREGS,%al
	jz      1f                              /* no, skip over */
	movl    %dr7,%eax                       /* yes, do the save */
	movl    %eax,PCB_DR7(%edx)
	andl    $0x0000fc00, %eax               /* disable all watchpoints */
	movl    %eax,%dr7
	movl    %dr6,%eax
	movl    %eax,PCB_DR6(%edx)
	movl    %dr3,%eax
	movl    %eax,PCB_DR3(%edx)
	movl    %dr2,%eax
	movl    %eax,PCB_DR2(%edx)
	movl    %dr1,%eax
	movl    %eax,PCB_DR1(%edx)
	movl    %dr0,%eax
	movl    %eax,PCB_DR0(%edx)
1:
 
	/*
	 * Save BGL nesting count.  Note that we hold the BGL with a
	 * count of at least 1 on entry to cpu_heavy_switch().
	 */
#ifdef SMP
	movl	_mp_lock, %eax
	/* XXX FIXME: we should be saving the local APIC TPR */
#ifdef DIAGNOSTIC
	cmpl	$FREE_LOCK, %eax		/* is it free? */
	je	badsw4				/* yes, bad medicine! */
#endif /* DIAGNOSTIC */
	andl	$COUNT_FIELD, %eax		/* clear CPU portion */
	movl	%eax, PCB_MPNEST(%edx)		/* store it */
#endif /* SMP */

	/*
	 * Save the FP state if we have used the FP.
	 */
#if NNPX > 0
	movl	P_THREAD(%ecx),%ecx
	cmpl	%ecx,_npxthread
	jne	1f
	addl	$PCB_SAVEFPU,%edx		/* h/w bugs make saving complicated */
	pushl	%edx
	call	_npxsave			/* do it in a big C function */
	popl	%eax
1:
	/* %ecx,%edx trashed */
#endif	/* NNPX > 0 */

	/*
	 * Switch to the next thread, which was passed as an argument
	 * to cpu_heavy_switch().  Due to the switch-restore function we pushed,
	 * the argument is at 8(%esp).  Set the current thread, load the
	 * stack pointer, and 'ret' into the switch-restore function.
	 */
	movl	8(%esp),%eax
	movl	%eax,_curthread
	movl	TD_SP(%eax),%esp
	ret

/*
 *  cpu_exit_switch()
 *
 *	The switch function is changed to this when a thread is going away
 *	for good.  We have to ensure that the MMU state is not cached, and
 *	we don't bother saving the existing thread state before switching.
 */
ENTRY(cpu_exit_switch)
	movl	_IdlePTD,%ecx
	movl	%cr3,%eax
	cmpl	%ecx,%eax
	je	1f
	movl	%ecx,%cr3
1:
	cli
	movl	4(%esp),%eax
	movl	%eax,_curthread
	movl	TD_SP(%eax),%esp
	ret

/*
 * cpu_heavy_restore()	(current thread in %eax on entry)
 *
 *	Restore the thread after an LWKT switch.  This entry is normally
 *	called via the LWKT switch restore function, which was pulled 
 *	off the thread stack and jumped to.
 *
 *	This entry is only called if the thread was previously saved
 *	using cpu_heavy_switch() (the heavy weight process thread switcher).
 *
 *	YYY theoretically we do not have to restore everything here, a lot
 *	of this junk can wait until we return to usermode.  But for now
 *	we restore everything.
 *
 *	YYY STI/CLI sequencing.
 */
ENTRY(cpu_heavy_restore)
	/* interrupts are disabled */
	movl	TD_MACH+MTD_CPL(%eax),%edx
	movl	%edx,_cpl			/* YYY temporary */
	movl	TD_PCB(%eax),%edx		/* YYY temporary */
	movl	TD_PROC(%eax),%ecx
#ifdef	DIAGNOSTIC
	cmpb	$SRUN,P_STAT(%ecx)
	jne	badsw2
#endif

#if defined(SWTCH_OPTIM_STATS)
	incl	_swtch_optim_stats
#endif
	/*
	 * Restore the MMU address space
	 */
	movl	%cr3,%ebx
	cmpl	PCB_CR3(%edx),%ebx
	je	4f
#if defined(SWTCH_OPTIM_STATS)
	decl	_swtch_optim_stats
	incl	_tlb_flush_count
#endif
	movl	PCB_CR3(%edx),%ebx
	movl	%ebx,%cr3
4:

	/*
	 * Deal with the PCB extension, restore the private tss
	 */
#ifdef SMP
	movl	_cpuid, %esi
#else
	xorl	%esi, %esi
#endif
	cmpl	$0, PCB_EXT(%edx)		/* has pcb extension? */
	je	1f
	btsl	%esi, _private_tss		/* mark use of private tss */
	movl	PCB_EXT(%edx), %edi		/* new tss descriptor */
	jmp	2f
1:

	/*
	 * update common_tss.tss_esp0 pointer.  This is the supervisor
	 * stack pointer on entry from user mode.  Since the pcb is
	 * at the top of the supervisor stack esp0 starts just below it.
	 * We leave enough space for vm86 (16 bytes).
	 *
	 * common_tss.tss_esp0 is needed when user mode traps into the
	 * kernel.
	 */
	leal	-16(%edx),%ebx
	movl	%ebx, _common_tss + TSS_ESP0

	btrl	%esi, _private_tss
	jae	3f
#ifdef SMP
	movl	$gd_common_tssd, %edi
	addl	%fs:0, %edi
#else
	movl	$_common_tssd, %edi
#endif
	/*
	 * Move the correct TSS descriptor into the GDT slot, then reload
	 * tr.   YYY not sure what is going on here
	 */
2:
	movl	_tss_gdt, %ebx			/* entry in GDT */
	movl	0(%edi), %eax
	movl	%eax, 0(%ebx)
	movl	4(%edi), %eax
	movl	%eax, 4(%ebx)
	movl	$GPROC0_SEL*8, %esi		/* GSEL(entry, SEL_KPL) */
	ltr	%si

	/*
	 * Tell the pmap that our cpu is using the VMSPACE now.
	 */
3:
	movl	P_VMSPACE(%ecx), %ebx
#ifdef SMP
	movl	_cpuid, %eax
#else
	xorl	%eax, %eax
#endif
	btsl	%eax, VM_PMAP+PM_ACTIVE(%ebx)

	/*
	 * Restore general registers.
	 */
	movl	PCB_EBX(%edx),%ebx
	movl	PCB_ESP(%edx),%esp
	movl	PCB_EBP(%edx),%ebp
	movl	PCB_ESI(%edx),%esi
	movl	PCB_EDI(%edx),%edi
	movl	PCB_EIP(%edx),%eax
	movl	%eax,(%esp)

	/*
	 * SMP ickyness to direct interrupts.
	 */

#ifdef SMP
#ifdef GRAB_LOPRIO				/* hold LOPRIO for INTs */
#ifdef CHEAP_TPR
	movl	$0, lapic_tpr
#else
	andl	$~APIC_TPR_PRIO, lapic_tpr
#endif /** CHEAP_TPR */
#endif /** GRAB_LOPRIO */
	movl	_cpuid,%eax
	movb	%al, P_ONCPU(%ecx)
#endif /* SMP */

	/*
	 * Restore the BGL nesting count.  Note that the nesting count will
	 * be at least 1.
	 */
#ifdef SMP
	movl	_cpu_lockid, %eax
	orl	PCB_MPNEST(%edx), %eax		/* add next count from PROC */
	movl	%eax, _mp_lock			/* load the mp_lock */
	/* XXX FIXME: we should be restoring the local APIC TPR */
#endif /* SMP */

	/*
	 * Restore the user LDT if we have one
	 */
#ifdef	USER_LDT
	cmpl	$0, PCB_USERLDT(%edx)
	jnz	1f
	movl	__default_ldt,%eax
	cmpl	_currentldt,%eax
	je	2f
	lldt	__default_ldt
	movl	%eax,_currentldt
	jmp	2f
1:	pushl	%edx
	call	_set_user_ldt
	popl	%edx
2:
#endif
	/*
	 * Restore the %gs segment register, which must be done after
	 * loading the user LDT.  Since user processes can modify the
	 * register via procfs, this may result in a fault which is
	 * detected by checking the fault address against cpu_switch_load_gs
	 * in i386/i386/trap.c
	 */
	.globl	cpu_switch_load_gs
cpu_switch_load_gs:
	movl	PCB_GS(%edx),%gs

	/*
	 * Restore the DEBUG register state if necessary.
	 */
	movb    PCB_FLAGS(%edx),%al
	andb    $PCB_DBREGS,%al
	jz      1f                              /* no, skip over */
	movl    PCB_DR6(%edx),%eax              /* yes, do the restore */
	movl    %eax,%dr6
	movl    PCB_DR3(%edx),%eax
	movl    %eax,%dr3
	movl    PCB_DR2(%edx),%eax
	movl    %eax,%dr2
	movl    PCB_DR1(%edx),%eax
	movl    %eax,%dr1
	movl    PCB_DR0(%edx),%eax
	movl    %eax,%dr0
	movl	%dr7,%eax                /* load dr7 so as not to disturb */
	andl    $0x0000fc00,%eax         /*   reserved bits               */
	pushl   %ebx
	movl    PCB_DR7(%edx),%ebx
	andl	$~0x0000fc00,%ebx
	orl     %ebx,%eax
	popl	%ebx
	movl    %eax,%dr7
1:
#if 0
	/*
	 * Remove the heavy weight process from the heavy weight queue.
	 * this will also have the side effect of removing the thread from
	 * the run queue.  YYY temporary?
	 *
	 * LWKT threads stay on the run queue until explicitly removed.
	 */
	pushl	%ecx
	call	remrunqueue
	addl	$4,%esp
#endif

	sti			/* XXX */
	ret

CROSSJUMPTARGET(sw1a)

#ifdef DIAGNOSTIC
badsw1:
	pushl	$sw0_1
	call	_panic

sw0_1:	.asciz	"cpu_switch: has wchan"

badsw2:
	pushl	$sw0_2
	call	_panic

sw0_2:	.asciz	"cpu_switch: not SRUN"
#endif

#if defined(SMP) && defined(DIAGNOSTIC)
badsw4:
	pushl	$sw0_4
	call	_panic

sw0_4:	.asciz	"cpu_switch: do not have lock"
#endif /* SMP && DIAGNOSTIC */

/*
 * savectx(pcb)
 * Update pcb, saving current processor state.
 */
ENTRY(savectx)
	/* fetch PCB */
	movl	4(%esp),%ecx

	/* caller's return address - child won't execute this routine */
	movl	(%esp),%eax
	movl	%eax,PCB_EIP(%ecx)

	movl	%cr3,%eax
	movl	%eax,PCB_CR3(%ecx)

	movl	%ebx,PCB_EBX(%ecx)
	movl	%esp,PCB_ESP(%ecx)
	movl	%ebp,PCB_EBP(%ecx)
	movl	%esi,PCB_ESI(%ecx)
	movl	%edi,PCB_EDI(%ecx)
	movl	%gs,PCB_GS(%ecx)

#if NNPX > 0
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
	movl	_npxthread,%eax
	testl	%eax,%eax
	je	1f

	pushl	%ecx
	movl	TD_PCB(%eax),%eax
	leal	PCB_SAVEFPU(%eax),%eax
	pushl	%eax
	pushl	%eax
	call	_npxsave
	addl	$4,%esp
	popl	%eax
	popl	%ecx

	pushl	$PCB_SAVEFPU_SIZE
	leal	PCB_SAVEFPU(%ecx),%ecx
	pushl	%ecx
	pushl	%eax
	call	_bcopy
	addl	$12,%esp
#endif	/* NNPX > 0 */

1:
	ret

/*
 * cpu_idle_restore()	(current thread in %eax on entry)
 *
 *	Don't bother setting up any regs other then %ebp so backtraces
 *	don't die.  This restore function is used to bootstrap into the
 *	cpu_idle() LWKT only, after that cpu_lwkt_*() will be used for
 *	switching.
 */
ENTRY(cpu_idle_restore)
	movl	$0,%ebp
	pushl	$0
	jmp	cpu_idle

/*
 * cpu_lwkt_switch()
 *
 *	Standard LWKT switching function.  Only non-scratch registers are
 *	saved and we don't bother with the MMU state or anything else.
 *	YYY BGL, SPL
 */
ENTRY(cpu_lwkt_switch)
	movl	4(%esp),%eax
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	pushfl
	movl	_curthread,%ecx
	movl	_cpl,%edx			/* YYY temporary */
	movl	%edx,TD_MACH+MTD_CPL(%ecx)	/* YYY temporary */
	pushl	$cpu_lwkt_restore
	cli
	movl	%esp,TD_SP(%ecx)
	movl	%eax,_curthread
	movl	TD_SP(%eax),%esp
	ret

/*
 * cpu_idle_restore()	(current thread in %eax on entry)
 *
 *	Don't bother setting up any regs other then %ebp so backtraces
 *	don't die.
 */
ENTRY(cpu_lwkt_restore)
	popfl
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	movl	TD_MACH+MTD_CPL(%eax),%ecx	/* YYY temporary */
	movl	%ecx,_cpl			/* YYY temporary */
	ret

