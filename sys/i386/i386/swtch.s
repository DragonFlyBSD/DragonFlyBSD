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
 * $DragonFly: src/sys/i386/i386/Attic/swtch.s,v 1.21 2003/07/06 21:23:48 dillon Exp $
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

	.globl	panic

#if defined(SWTCH_OPTIM_STATS)
	.globl	swtch_optim_stats, tlb_flush_count
swtch_optim_stats:	.long	0		/* number of _swtch_optims */
tlb_flush_count:	.long	0
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
	movl	PCPU(curthread),%ecx
	movl	TD_PROC(%ecx),%ecx

	cli
	movl	P_VMSPACE(%ecx), %edx
	movl	PCPU(cpuid), %eax
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
	 * Save the FP state if we have used the FP.
	 */
#if NNPX > 0
	movl	P_THREAD(%ecx),%ecx
	cmpl	%ecx,PCPU(npxthread)
	jne	1f
	addl	$PCB_SAVEFPU,%edx		/* h/w bugs make saving complicated */
	pushl	%edx
	call	npxsave			/* do it in a big C function */
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
	movl	%eax,PCPU(curthread)
	movl	TD_SP(%eax),%esp
	ret

/*
 *  cpu_exit_switch()
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
	movl	IdlePTD,%ecx
	movl	%cr3,%eax
	cmpl	%ecx,%eax
	je	1f
	movl	%ecx,%cr3
1:
	movl	PCPU(curthread),%ecx
	/*
	 * Switch to the next thread.
	 */
	cli
	movl	4(%esp),%eax
	movl	%eax,PCPU(curthread)
	movl	TD_SP(%eax),%esp

	/*
	 * We are now the next thread, set the exited flag and wakeup
	 * any waiters.
	 */
	orl	$TDF_EXITED,TD_FLAGS(%ecx)
#if 0			/* YYY MP lock may not be held by new target */
	pushl	%eax
	pushl	%ecx	/* wakeup(oldthread) */
	call	wakeup
	addl	$4,%esp
	popl	%eax	/* note: next thread expects curthread in %eax */
#endif

	/*
	 * Restore the next thread's state and resume it.  Note: the
	 * restore function assumes that the next thread's address is
	 * in %eax.
	 */
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
 *
 *	YYY note: spl check is done in mi_switch when it splx()'s.
 */

ENTRY(cpu_heavy_restore)
	/* interrupts are disabled */
	movl	TD_PCB(%eax),%edx
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
	movl	PCPU(cpuid), %esi
	cmpl	$0, PCB_EXT(%edx)		/* has pcb extension? */
	je	1f
	btsl	%esi, private_tss		/* mark use of private tss */
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
	movl	%ebx, PCPU(common_tss) + TSS_ESP0

	btrl	%esi, private_tss
	jae	3f

	/*
	 * There is no way to get the address of a segment-accessed variable
	 * so we store a self-referential pointer at the base of the per-cpu
	 * data area and add the appropriate offset.
	 */
	movl	$gd_common_tssd, %edi
	addl	%fs:0, %edi

	/*
	 * Move the correct TSS descriptor into the GDT slot, then reload
	 * tr.   YYY not sure what is going on here
	 */
2:
	movl	PCPU(tss_gdt), %ebx		/* entry in GDT */
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
	movl	PCPU(cpuid), %eax
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
	 * Restore the user LDT if we have one
	 */
#ifdef	USER_LDT
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

	sti			/* XXX */
	ret

CROSSJUMPTARGET(sw1a)

badsw0:
	pushl	%eax
	pushl	$sw0_1
	call	panic

sw0_1:	.asciz	"cpu_switch: panic: %p"

#ifdef DIAGNOSTIC
badsw1:
	pushl	$sw0_1
	call	panic

sw0_1:	.asciz	"cpu_switch: has wchan"

badsw2:
	pushl	$sw0_2
	call	panic

sw0_2:	.asciz	"cpu_switch: not SRUN"
#endif

#if defined(SMP) && defined(DIAGNOSTIC)
badsw4:
	pushl	$sw0_4
	call	panic

sw0_4:	.asciz	"cpu_switch: do not have lock"
#endif /* SMP && DIAGNOSTIC */

string:	.asciz	"SWITCHING\n"

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
	movl	PCPU(npxthread),%eax
	testl	%eax,%eax
	je	1f

	pushl	%ecx
	movl	TD_PCB(%eax),%eax
	leal	PCB_SAVEFPU(%eax),%eax
	pushl	%eax
	pushl	%eax
	call	npxsave
	addl	$4,%esp
	popl	%eax
	popl	%ecx

	pushl	$PCB_SAVEFPU_SIZE
	leal	PCB_SAVEFPU(%ecx),%ecx
	pushl	%ecx
	pushl	%eax
	call	bcopy
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
 *
 *	If we are an AP we have to call ap_init() before jumping to
 *	cpu_idle().  ap_init() will synchronize with the BP and finish
 *	setting up various ncpu-dependant globaldata fields.  This may
 *	happen on UP as well as SMP if we happen to be simulating multiple
 *	cpus.
 */
ENTRY(cpu_idle_restore)
	movl	$0,%ebp
	pushl	$0
#ifdef SMP
	cmpl	$0,PCPU(cpuid)
	je	1f
	call	ap_init
1:
#endif
	sti
	jmp	cpu_idle

/*
 * cpu_kthread_restore()	(current thread is %eax on entry)
 *
 *	Don't bother setting up any regs other then %ebp so backtraces
 *	don't die.  This restore function is used to bootstrap into an
 *	LWKT based kernel thread only.  cpu_lwkt_switch() will be used
 *	after this.
 *
 *	Since all of our context is on the stack we are reentrant and
 *	we can release our critical section and enable interrupts early.
 */
ENTRY(cpu_kthread_restore)
	movl	TD_PCB(%eax),%ebx
	movl	$0,%ebp
	subl	$TDPRI_CRIT,TD_PRI(%eax)
	sti
	popl	%edx		/* kthread exit function */
	pushl	PCB_EBX(%ebx)	/* argument to ESI function */
	pushl	%edx		/* set exit func as return address */
	movl	PCB_ESI(%ebx),%eax
	jmp	*%eax

/*
 * cpu_lwkt_switch()
 *
 *	Standard LWKT switching function.  Only non-scratch registers are
 *	saved and we don't bother with the MMU state or anything else.
 *
 *	This function is always called while in a critical section.
 *
 *	YYY BGL, SPL
 */
ENTRY(cpu_lwkt_switch)
	movl	4(%esp),%eax
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	pushfl
	movl	PCPU(curthread),%ecx
	pushl	$cpu_lwkt_restore
	cli
	movl	%esp,TD_SP(%ecx)
	movl	%eax,PCPU(curthread)
	movl	TD_SP(%eax),%esp
	ret

/*
 * cpu_lwkt_restore()	(current thread in %eax on entry)
 *
 *	Standard LWKT restore function.  This function is always called
 *	while in a critical section.
 *	
 *	Warning: due to preemption the restore function can be used to 
 *	'return' to the original thread.  Interrupt disablement must be
 *	protected through the switch so we cannot run splz here.
 */
ENTRY(cpu_lwkt_restore)
	popfl
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret

