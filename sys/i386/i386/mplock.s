/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/i386/i386/mplock.s,v 1.29.2.2 2000/05/16 06:58:06 dillon Exp $
 * $DragonFly: src/sys/i386/i386/Attic/mplock.s,v 1.4 2003/07/06 21:23:48 dillon Exp $
 *
 * Functions for locking between CPUs in a SMP system.
 *
 * This is an "exclusive counting semaphore".  This means that it can be
 * free (0xffffffff) or be owned by a CPU (0xXXYYYYYY where XX is CPU-id
 * and YYYYYY is the count).
 *
 * Contrary to most implementations around, this one is entirely atomic:
 * The attempt to seize/release the semaphore and the increment/decrement
 * is done in one atomic operation.  This way we are safe from all kinds
 * of weird reentrancy situations.
 */

#include <machine/asmacros.h>
#include <machine/smptests.h>		/** GRAB_LOPRIO */
#include <machine/apic.h>

#include "assym.s"

	.data
	ALIGN_DATA
#ifdef SMP
	.globl	mp_lock
mp_lock:
	.long	-1			/* initialized to not held */
#endif

	.text
	SUPERALIGN_TEXT

	/*
	 * Note on cmpxchgl... exchanges ecx with mem if mem matches eax.
	 * Z=1 (jz) on success. 
	 */
NON_GPROF_ENTRY(cpu_get_initial_mplock)
	movl	PCPU(curthread),%ecx
	movl	$1,TD_MPCOUNT(%ecx)	/* curthread has mpcount of 1 */
	movl	$0,mp_lock		/* owned by cpu 0 */
	NON_GPROF_RET

	/*
	 * cpu_try_mplock() returns non-zero on success, 0 on failure.  It
	 * only adjusts mp_lock.  It does not touch td_mpcount, and it
	 * must be called from inside a critical section.
	 */
NON_GPROF_ENTRY(cpu_try_mplock)
	movl	PCPU(cpuid),%ecx
	movl	$-1,%eax
	cmpxchgl %ecx,mp_lock		/* ecx<->mem if eax matches */
	jnz	1f
	movl	$1,%eax
	NON_GPROF_RET
1:
	movl	$0,%eax
	NON_GPROF_RET

NON_GPROF_ENTRY(get_mplock)
	movl	PCPU(curthread),%edx
	cmpl	$0,TD_MPCOUNT(%edx)
	je	1f
	incl	TD_MPCOUNT(%edx)	/* already have it, just ++mpcount */
	NON_GPROF_RET
1:
	pushfl
	cli
	movl	$1,TD_MPCOUNT(%edx)
	movl	PCPU(cpuid),%ecx
	movl	$-1,%eax
	cmpxchgl %ecx,mp_lock		/* ecx<->mem & JZ if eax matches */
	jnz	2f
	popfl				/* success */
	NON_GPROF_RET
2:
	movl	PCPU(cpuid),%eax	/* failure */
	cmpl	%eax,mp_lock
	je	badmp_get
	popfl
	jmp	lwkt_switch		/* will be correct on return */

NON_GPROF_ENTRY(try_mplock)
	movl	PCPU(curthread),%edx
	cmpl	$0,TD_MPCOUNT(%edx)
	je	1f
	incl	TD_MPCOUNT(%edx)	/* already have it, just ++mpcount */
	movl	$1,%eax
	NON_GPROF_RET
1:
	pushfl
	cli
	movl	PCPU(cpuid),%ecx
	movl	$-1,%eax
	cmpxchgl %ecx,mp_lock		/* ecx<->mem & JZ if eax matches */
	jnz	2f
	movl	$1,TD_MPCOUNT(%edx)
	popfl				/* success */
	movl	$1,%eax
	NON_GPROF_RET
2:
	movl	PCPU(cpuid),%eax	/* failure */
	cmpl	%eax,mp_lock
	je	badmp_get
	popfl
	movl	$0,%eax
	NON_GPROF_RET

NON_GPROF_ENTRY(rel_mplock)
	movl	PCPU(curthread),%edx
	cmpl	$1,TD_MPCOUNT(%edx)
	je	1f
	subl	$1,TD_MPCOUNT(%edx)
	NON_GPROF_RET
1:
	pushfl
	cli
	movl	$0,TD_MPCOUNT(%edx)
	movl	$MP_FREE_LOCK,mp_lock
	popfl
	NON_GPROF_RET

badmp_get:
	pushl	$bmpsw1
	call	panic
badmp_rel:
	pushl	$bmpsw2
	call	panic

	.data

bmpsw1:
	.asciz	"try/get_mplock(): already have lock!"

bmpsw2:
	.asciz	"rel_mplock(): not holding lock!"

#if 0
/* after 1st acquire of lock we grab all hardware INTs */
#ifdef GRAB_LOPRIO
#define GRAB_HWI	movl	$ALLHWI_LEVEL, lapic_tpr

/* after last release of lock give up LOW PRIO (ie, arbitrate INTerrupts) */
#define ARB_HWI		movl	$LOPRIO_LEVEL, lapic_tpr /* CHEAP_TPR */
#endif
#endif

