/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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

#include "use_npx.h"

#include <sys/rtprio.h>

#include <machine/asmacros.h>
#include <machine/segments.h>

#include <machine/pmap.h>
#include <machine_base/apic/apicreg.h>
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
 * cpu_heavy_switch(next_thread)
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
	 * Save general regs
	 */
	movl	PCPU(curthread),%ecx
	movl	(%esp),%eax			/* (reorder optimization) */
	movl	TD_PCB(%ecx),%edx		/* EDX = PCB */
	movl	%eax,PCB_EIP(%edx)		/* return PC may be modified */
	movl	%ebx,PCB_EBX(%edx)
	movl	%esp,PCB_ESP(%edx)
	movl	%ebp,PCB_EBP(%edx)
	movl	%esi,PCB_ESI(%edx)
	movl	%edi,PCB_EDI(%edx)
	movl	4(%esp),%edi			/* EDI = newthread */

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
	movl	%ecx,%ebx			/* EBX = oldthread */
	movl	TD_LWP(%ecx),%ecx		/* ECX = oldlwp */
	movl	TD_LWP(%edi),%esi		/* ESI = newlwp */
	movl	LWP_VMSPACE(%ecx),%ecx		/* ECX = oldvmspace */
	testl	%esi,%esi			/* might not be a heavy */
	jz	1f
	cmpl	LWP_VMSPACE(%esi),%ecx		/* same vmspace? */
	je	2f
1:
	movl	PCPU(cpuid), %eax
	MPLOCKED btrl	%eax, VM_PMAP+PM_ACTIVE(%ecx)
2:
	/*
	 * Push the LWKT switch restore function, which resumes a heavy
	 * weight process.  Note that the LWKT switcher is based on
	 * TD_SP, while the heavy weight process switcher is based on
	 * PCB_ESP.  TD_SP is usually two ints pushed relative to
	 * PCB_ESP.  We push the flags for later restore by cpu_heavy_restore.
	 */
	pushfl
	pushl	$cpu_heavy_restore
	movl	%esp,TD_SP(%ebx)

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
 
#if NNPX > 0
	/*
	 * Save the FP state if we have used the FP.  Note that calling
	 * npxsave will NULL out PCPU(npxthread).
	 */
	cmpl	%ebx,PCPU(npxthread)
	jne	1f
	pushl	TD_SAVEFPU(%ebx)
	call	npxsave			/* do it in a big C function */
	addl	$4,%esp			/* EAX, ECX, EDX trashed */
1:
#endif	/* NNPX > 0 */

	/*
	 * Switch to the next thread, which was passed as an argument
	 * to cpu_heavy_switch().  Due to the eflags and switch-restore
	 * function we pushed, the argument is at 12(%esp).  Set the current
	 * thread, load the stack pointer, and 'ret' into the switch-restore
	 * function.
	 *
	 * The switch restore function expects the new thread to be in %eax
	 * and the old one to be in %ebx.
	 *
	 * There is a one-instruction window where curthread is the new
	 * thread but %esp still points to the old thread's stack, but
	 * we are protected by a critical section so it is ok.
	 */
	movl	%edi,%eax		/* EAX = newtd, EBX = oldtd */
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
	movl	PCPU(curthread),%ebx

	/*
	 * If this is a process/lwp, deactivate the pmap after we've
	 * switched it out.
	 */
	movl	TD_LWP(%ebx),%ecx
	testl	%ecx,%ecx
	jz	2f
	movl	PCPU(cpuid), %eax
	movl	LWP_VMSPACE(%ecx), %ecx		/* ECX = vmspace */
	MPLOCKED btrl	%eax, VM_PMAP+PM_ACTIVE(%ecx)
2:
	/*
	 * Switch to the next thread.  RET into the restore function, which
	 * expects the new thread in EAX and the old in EBX.
	 *
	 * There is a one-instruction window where curthread is the new
	 * thread but %esp still points to the old thread's stack, but
	 * we are protected by a critical section so it is ok.
	 */
	movl	4(%esp),%eax
	movl	%eax,PCPU(curthread)
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
	popfl
	movl	TD_LWP(%eax),%ecx

#if defined(SWTCH_OPTIM_STATS)
	incl	_swtch_optim_stats
#endif
	/*
	 * Tell the pmap that our cpu is using the VMSPACE now.  We cannot
	 * safely test/reload %cr3 until after we have set the bit in the
	 * pmap (remember, we do not hold the MP lock in the switch code).
	 *
	 * Also note that when switching between two lwps sharing the
	 * same vmspace we have already avoided clearing the cpu bit
	 * in pm_active.  If we had cleared it other cpus would not know
	 * to IPI us and we would have to unconditionally reload %cr3.
	 *
	 * Also note that if the pmap is undergoing an atomic inval/mod
	 * that is unaware that our cpu has been added to it we have to
	 * wait for it to complete before we can continue.
	 */
	movl	LWP_VMSPACE(%ecx), %ecx		/* ECX = vmspace */
	movl	PCPU(cpumask), %esi
	MPLOCKED orl %esi, VM_PMAP+PM_ACTIVE(%ecx)

	movl	VM_PMAP+PM_ACTIVE_LOCK(%ecx),%esi
	testl	$CPULOCK_EXCL,%esi
	jz	1f
	pushl	%eax				/* save curthread */
	pushl	%ecx				/* call(stack:vmspace) */
	call	pmap_interlock_wait
	popl	%ecx
	popl	%eax

	/*
	 * Needs unconditional load cr3
	 */
	movl	TD_PCB(%eax),%edx		/* EDX = PCB */
	movl	PCB_CR3(%edx),%ecx
	jmp	2f
1:

	/*
	 * Restore the MMU address space.  If it is the same as the last
	 * thread we don't have to invalidate the tlb (i.e. reload cr3).
	 * YYY which naturally also means that the PM_ACTIVE bit had better
	 * already have been set before we set it above, check? YYY
	 */
	movl	TD_PCB(%eax),%edx		/* EDX = PCB */
	movl	%cr3,%esi
	movl	PCB_CR3(%edx),%ecx
	cmpl	%esi,%ecx
	je	4f
2:
#if defined(SWTCH_OPTIM_STATS)
	decl	_swtch_optim_stats
	incl	_tlb_flush_count
#endif
	movl	%ecx,%cr3
4:

	/*
	 * NOTE: %ebx is the previous thread and %eax is the new thread.
	 *	 %ebx is retained throughout so we can return it.
	 *
	 *	 lwkt_switch[_return] is responsible for handling TDF_RUNNING.
	 */

	/*
	 * Deal with the PCB extension, restore the private tss
	 */
	movl	PCB_EXT(%edx),%edi	/* check for a PCB extension */
	movl	$1,%ecx			/* maybe mark use of a private tss */
	testl	%edi,%edi
	jnz	2f

	/*
	 * Going back to the common_tss.  We may need to update TSS_ESP0
	 * which sets the top of the supervisor stack when entering from
	 * usermode.  The PCB is at the top of the stack but we need another
	 * 16 bytes to take vm86 into account.
	 */
	leal	-16(%edx),%ecx
	movl	%ecx, PCPU(common_tss) + TSS_ESP0

	cmpl	$0,PCPU(private_tss)	/* don't have to reload if      */
	je	3f			/* already using the common TSS */

	subl	%ecx,%ecx		/* unmark use of private tss */

	/*
	 * Get the address of the common TSS descriptor for the ltr.
	 * There is no way to get the address of a segment-accessed variable
	 * so we store a self-referential pointer at the base of the per-cpu
	 * data area and add the appropriate offset.
	 */
	movl	$gd_common_tssd, %edi
	addl	%fs:0, %edi

	/*
	 * Move the correct TSS descriptor into the GDT slot, then reload
	 * ltr.
	 */
2:
	movl	%ecx,PCPU(private_tss)		/* mark/unmark private tss */
	movl	PCPU(tss_gdt), %ecx		/* entry in GDT */
	movl	0(%edi), %eax
	movl	%eax, 0(%ecx)
	movl	4(%edi), %eax
	movl	%eax, 4(%ecx)
	movl	$GPROC0_SEL*8, %esi		/* GSEL(entry, SEL_KPL) */
	ltr	%si

3:
	/*
	 * Restore general registers.  %ebx is restored later.
	 */
	movl	PCB_ESP(%edx),%esp
	movl	PCB_EBP(%edx),%ebp
	movl	PCB_ESI(%edx),%esi
	movl	PCB_EDI(%edx),%edi
	movl	PCB_EIP(%edx),%eax
	movl	%eax,(%esp)

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
	/*
	 * Restore the user TLS if we have one
	 */
	pushl	%edx
	call	set_user_TLS
	popl	%edx

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
	movl    PCB_DR7(%edx),%ecx
	andl	$~0x0000fc00,%ecx
	orl     %ecx,%eax
	movl    %eax,%dr7
1:
	movl	%ebx,%eax		/* return previous thread */
	movl	PCB_EBX(%edx),%ebx
	ret

/*
 * savectx(pcb)
 *
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

	pushl	%ecx			/* target pcb */
	movl	TD_SAVEFPU(%eax),%eax	/* originating savefpu area */
	pushl	%eax

	pushl	%eax
	call	npxsave
	addl	$4,%esp

	popl	%eax
	popl	%ecx

	pushl	$PCB_SAVEFPU_SIZE
	leal    PCB_SAVEFPU(%ecx),%ecx
	pushl	%ecx
	pushl	%eax
	call	bcopy
	addl	$12,%esp
#endif	/* NNPX > 0 */

1:
	ret

/*
 * cpu_idle_restore()	(current thread in %eax on entry) (one-time execution)
 *
 *	Don't bother setting up any regs other then %ebp so backtraces
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
	movl	IdlePTD,%ecx
	movl	$0,%ebp
	pushl	$0
	movl	%ecx,%cr3
	cmpl	$0,PCPU(cpuid)
	je	1f
	andl	$~TDF_RUNNING,TD_FLAGS(%ebx)
	orl	$TDF_RUNNING,TD_FLAGS(%eax)	/* manual, no switch_return */
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
	pushl	%eax
	pushl	%ebx	/* argument to lwkt_switch_return */
	call	lwkt_switch_return
	addl	$4,%esp
	popl	%eax
	jmp	cpu_idle

/*
 * cpu_kthread_restore() (current thread is %eax on entry) (one-time execution)
 *
 *	Don't bother setting up any regs other then %ebp so backtraces
 *	don't die.  This restore function is used to bootstrap into an
 *	LWKT based kernel thread only.  cpu_lwkt_switch() will be used
 *	after this.
 *
 *	Since all of our context is on the stack we are reentrant and
 *	we can release our critical section and enable interrupts early.
 *
 *	Because this switch target does not 'return' to lwkt_switch()
 *	we have to call lwkt_switch_return(otd) to clean up otd.
 *	otd is in %ebx.
 */
ENTRY(cpu_kthread_restore)
	sti
	movl	IdlePTD,%ecx
	movl	TD_PCB(%eax),%esi
	movl	$0,%ebp
	movl	%ecx,%cr3

	pushl	%eax
	pushl	%ebx	/* argument to lwkt_switch_return */
	call	lwkt_switch_return
	addl	$4,%esp
	popl	%eax
	decl	TD_CRITCOUNT(%eax)
	popl	%eax		/* kthread exit function */
	pushl	PCB_EBX(%esi)	/* argument to ESI function */
	pushl	%eax		/* set exit func as return address */
	movl	PCB_ESI(%esi),%eax
	jmp	*%eax

/*
 * cpu_lwkt_switch()
 *
 *	Standard LWKT switching function.  Only non-scratch registers are
 *	saved and we don't bother with the MMU state or anything else.
 *
 *	This function is always called while in a critical section.
 *
 *	There is a one-instruction window where curthread is the new
 *	thread but %esp still points to the old thread's stack, but
 *	we are protected by a critical section so it is ok.
 *
 *	YYY BGL, SPL
 */
ENTRY(cpu_lwkt_switch)
	pushl	%ebp	/* note: GDB hacked to locate ebp relative to td_sp */
	pushl	%ebx
	movl	PCPU(curthread),%ebx
	pushl	%esi
	pushl	%edi
	pushfl
	/* warning: adjust movl into %eax below if you change the pushes */

#if NNPX > 0
	/*
	 * Save the FP state if we have used the FP.  Note that calling
	 * npxsave will NULL out PCPU(npxthread).
	 *
	 * We have to deal with the FP state for LWKT threads in case they
	 * happen to get preempted or block while doing an optimized
	 * bzero/bcopy/memcpy.
	 */
	cmpl	%ebx,PCPU(npxthread)
	jne	1f
	pushl	TD_SAVEFPU(%ebx)
	call	npxsave			/* do it in a big C function */
	addl	$4,%esp			/* EAX, ECX, EDX trashed */
1:
#endif	/* NNPX > 0 */

	movl	4+20(%esp),%eax		/* switch to this thread */
	pushl	$cpu_lwkt_restore
	movl	%esp,TD_SP(%ebx)
	movl	%eax,PCPU(curthread)
	movl	TD_SP(%eax),%esp

	/*
	 * eax contains new thread, ebx contains old thread.
	 */
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
 *
 *	YYY we theoretically do not need to load IdlePTD into cr3, but if
 *	so we need a way to detect when the PTD we are using is being 
 *	deleted due to a process exiting.
 */
ENTRY(cpu_lwkt_restore)
	movl	IdlePTD,%ecx	/* YYY borrow but beware desched/cpuchg/exit */
	movl	%cr3,%edx
	cmpl	%ecx,%edx
	je	1f
	movl	%ecx,%cr3
1:
	/*
	 * NOTE: %ebx is the previous thread and %eax is the new thread.
	 *	 %ebx is retained throughout so we can return it.
	 *
	 *	 lwkt_switch[_return] is responsible for handling TDF_RUNNING.
	 */
	movl	%ebx,%eax
	popfl
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret

