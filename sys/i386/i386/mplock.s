/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/i386/i386/mplock.s,v 1.29.2.2 2000/05/16 06:58:06 dillon Exp $
 * $DragonFly: src/sys/i386/i386/Attic/mplock.s,v 1.6 2003/07/10 04:47:53 dillon Exp $
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

/*
 * YYY Debugging only.  Define this to be paranoid about invalidating the
 * TLB when we get giant.
 */
#undef PARANOID_INVLTLB

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
	 * Z=1 (jz) on success.   A lock prefix is required for MP.
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
	lock cmpxchgl %ecx,mp_lock	/* ecx<->mem if eax matches */
	jnz	1f
#ifdef PARANOID_INVLTLB
	movl	%cr3,%eax; movl %eax,%cr3	/* YYY check and remove */
#endif
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
#ifdef INVARIANTS
	movl	PCPU(cpuid),%eax	/* failure */
	cmpl	%eax,mp_lock
	jne	4f
#endif
	NON_GPROF_RET
1:
	pushfl
	cli
	movl	$1,TD_MPCOUNT(%edx)
	movl	PCPU(cpuid),%ecx
	movl	$-1,%eax
	lock cmpxchgl %ecx,mp_lock	/* ecx<->mem & JZ if eax matches */
	jnz	2f
#ifdef PARANOID_INVLTLB
	movl	%cr3,%eax; movl %eax,%cr3	/* YYY check and remove */
#endif
	popfl				/* success */
	NON_GPROF_RET
2:
#ifdef INVARIANTS
	movl	PCPU(cpuid),%eax	/* failure */
	cmpl	%eax,mp_lock
	je	3f
#endif
	addl	$TDPRI_CRIT,TD_PRI(%edx)
	popfl
	call	lwkt_switch		/* will be correct on return */
#ifdef INVARIANTS
	movl	PCPU(cpuid),%eax	/* failure */
	cmpl	%eax,mp_lock
	jne	4f
#endif
	movl	PCPU(curthread),%edx
	subl	$TDPRI_CRIT,TD_PRI(%edx)
	NON_GPROF_RET
3:
	cmpl	$0,panicstr		/* don't double panic */
	je	badmp_get
	popfl
	NON_GPROF_RET

4:
	cmpl	$0,panicstr		/* don't double panic */
	je	badmp_get2
	NON_GPROF_RET

NON_GPROF_ENTRY(try_mplock)
	movl	PCPU(curthread),%edx
	cmpl	$0,TD_MPCOUNT(%edx)
	je	1f
	incl	TD_MPCOUNT(%edx)	/* already have it, just ++mpcount */
#ifdef INVARIANTS
	movl	PCPU(cpuid),%eax	/* failure */
	cmpl	%eax,mp_lock
	jne	4b
#endif
	movl	$1,%eax
	NON_GPROF_RET
1:
	pushfl
	cli
	movl	PCPU(cpuid),%ecx
	movl	$-1,%eax
	lock cmpxchgl %ecx,mp_lock	/* ecx<->mem & JZ if eax matches */
	jnz	2f
	movl	$1,TD_MPCOUNT(%edx)
#ifdef PARANOID_INVLTLB
	movl	%cr3,%eax; movl %eax,%cr3	/* YYY check and remove */
#endif
	popfl				/* success */
	movl	$1,%eax
	NON_GPROF_RET
2:
#ifdef INVARIANTS
	cmpl	$0,panicstr
	jnz	3f
	movl	PCPU(cpuid),%eax	/* failure */
	cmpl	%eax,mp_lock
	je	badmp_get
3:
#endif
	popfl
	movl	$0,%eax
	NON_GPROF_RET

NON_GPROF_ENTRY(rel_mplock)
	movl	PCPU(curthread),%edx
	movl	TD_MPCOUNT(%edx),%eax
	cmpl	$1,%eax
	je	1f
#ifdef INVARIANTS
	testl	%eax,%eax
	jz	badmp_rel
#endif
	subl	$1,%eax
	movl	%eax,TD_MPCOUNT(%edx)
	NON_GPROF_RET
1:
	pushfl
	cli
#ifdef INVARIANTS
	movl	PCPU(cpuid),%ecx
	cmpl	%ecx,mp_lock
	jne	badmp_rel2
#endif
	movl	$0,TD_MPCOUNT(%edx)
	movl	$MP_FREE_LOCK,mp_lock
	popfl
	NON_GPROF_RET

#ifdef INVARIANTS

badmp_get:
	pushl	$bmpsw1
	call	panic
badmp_get2:
	pushl	$bmpsw1a
	call	panic
badmp_rel:
	pushl	$bmpsw2
	call	panic
badmp_rel2:
	pushl	$bmpsw2a
	call	panic

	.data

bmpsw1:
	.asciz	"try/get_mplock(): already have lock! %d %p"

bmpsw1a:
	.asciz	"try/get_mplock(): failed on count or switch %d %p"

bmpsw2:
	.asciz	"rel_mplock(): mpcount already 0 @ %p %p %p %p %p %p %p %p!"

bmpsw2a:
	.asciz	"rel_mplock(): Releasing another cpu's MP lock! %p %p"

#endif

#if 0
/* after 1st acquire of lock we grab all hardware INTs */
#ifdef GRAB_LOPRIO
#define GRAB_HWI	movl	$ALLHWI_LEVEL, lapic_tpr

/* after last release of lock give up LOW PRIO (ie, arbitrate INTerrupts) */
#define ARB_HWI		movl	$LOPRIO_LEVEL, lapic_tpr /* CHEAP_TPR */
#endif
#endif

