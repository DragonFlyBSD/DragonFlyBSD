/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/i386/i386/mplock.s,v 1.29.2.2 2000/05/16 06:58:06 dillon Exp $
 * $DragonFly: src/sys/i386/i386/Attic/mplock.s,v 1.8 2003/07/10 18:36:13 dillon Exp $
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
	 * only adjusts mp_lock.  It does not touch td_mpcount.
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
	subl	%eax,%eax
	NON_GPROF_RET

	/*
	 * get_mplock() Obtains the MP lock and may switch away if it cannot
	 * get it.  Note that td_mpcount may not be synchronized with the
	 * actual state of the MP lock.  This situation occurs when 
	 * get_mplock() or try_mplock() is indirectly called from the
	 * lwkt_switch() code, or from a preemption (though, truthfully,
	 * only try_mplock() should ever be called in this fashion).  If
	 * we cannot get the MP lock we pre-dispose TD_MPCOUNT and call
	 * lwkt_swich().  The MP lock will be held on return.
	 *
	 * Note that both get_mplock() and try_mplock() must pre-dispose
	 * mpcount before attempting to get the lock, in case we get
	 * preempted.  This allows us to avoid expensive interrupt
	 * disablement instructions and allows us to be called from outside
	 * a critical section.
	 */
NON_GPROF_ENTRY(get_mplock)
	movl	PCPU(cpuid),%ecx
	movl	PCPU(curthread),%edx
	cmpl	%ecx,mp_lock
	jne	1f
	incl	TD_MPCOUNT(%edx)
	NON_GPROF_RET
1:
	incl	TD_MPCOUNT(%edx)
	movl	$-1,%eax
	lock cmpxchgl %ecx,mp_lock
	jnz	2f
#ifdef PARANOID_INVLTLB
	movl	%cr3,%eax; movl %eax,%cr3	/* YYY check and remove */
#endif
	NON_GPROF_RET
2:
	call	lwkt_switch		/* will be correct on return */
#ifdef INVARIANTS
	movl	PCPU(cpuid),%eax	/* failure */
	cmpl	%eax,mp_lock
	jne	4f
#endif
	NON_GPROF_RET
4:
	cmpl	$0,panicstr		/* don't double panic */
	je	badmp_get2
	NON_GPROF_RET

	/*
	 * try_mplock() attempts to obtain the MP lock and will not switch
	 * away if it cannot get it.  Note that td_mpcoutn may not be 
	 * synchronized with the actual state of the MP lock.
	 */
NON_GPROF_ENTRY(try_mplock)
	movl	PCPU(cpuid),%ecx
	movl	PCPU(curthread),%edx
	cmpl	%ecx,mp_lock
	jne	1f
	incl	TD_MPCOUNT(%edx)
	movl	$1,%eax
	NON_GPROF_RET
1:
	incl	TD_MPCOUNT(%edx)	/* pre-dispose */
	movl	$-1,%eax
	lock cmpxchgl %ecx,mp_lock
	jnz	2f
#ifdef PARANOID_INVLTLB
	movl	%cr3,%eax; movl %eax,%cr3	/* YYY check and remove */
#endif
	movl	$1,%eax
	NON_GPROF_RET
2:
	decl	TD_MPCOUNT(%edx)	/* un-dispose */
	subl	%eax,%eax
	NON_GPROF_RET

	/*
	 * rel_mplock() release the MP lock.  The MP lock MUST be held,
	 * td_mpcount must NOT be out of synch with the lock.  It is allowed
	 * for the physical lock to be released prior to setting the count
	 * to 0, preemptions will deal with the case (see lwkt_thread.c).
	 */
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
#ifdef INVARIANTS
	movl	PCPU(cpuid),%ecx
	cmpl	%ecx,mp_lock
	jne	badmp_rel2
#endif
	movl	$MP_FREE_LOCK,mp_lock
	movl	$0,TD_MPCOUNT(%edx)
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

