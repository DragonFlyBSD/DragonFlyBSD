/*
 * $FreeBSD: src/sys/i386/i386/mplock.s,v 1.29.2.2 2000/05/16 06:58:06 dillon Exp $
 * $DragonFly: src/sys/i386/i386/Attic/mplock.s,v 1.15 2004/11/20 20:50:33 dillon Exp $
 *
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
 *				DragonFly MPLOCK operation
 *
 * Each thread has an MP lock count, td_mpcount, and there is a shared
 * global called mp_lock.  mp_lock is the physical MP lock and contains either
 * -1 or the cpuid of the cpu owning the lock.  The count is *NOT* integrated
 * into mp_lock but instead resides in each thread td_mpcount.
 *
 * When obtaining or releasing the MP lock the td_mpcount is PREDISPOSED
 * to the desired count *PRIOR* to operating on the mp_lock itself.  MP
 * lock operations can occur outside a critical section with interrupts
 * enabled with the provisio (which the routines below handle) that an
 * interrupt may come along and preempt us, racing our cmpxchgl instruction
 * to perform the operation we have requested by pre-dispoing td_mpcount.
 *
 * Additionally, the LWKT threading system manages the MP lock and
 * lwkt_switch(), in particular, may be called after pre-dispoing td_mpcount
 * to handle 'blocking' on the MP lock.
 *
 *
 * Recoded from the FreeBSD original:
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 */

#include <machine/asmacros.h>
#include <machine/smptests.h>		/** GRAB_LOPRIO */
#include <machine/apicreg.h>

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
	 * only adjusts mp_lock, it does not touch td_mpcount.  Callers
	 * should always increment td_mpcount *before* trying to acquire
	 * the actual lock, predisposing td_mpcount to the desired state of
	 * the lock.
	 *
	 * NOTE! Only call cpu_try_mplock() inside a critical section.  If
	 * you don't an interrupt can come along and get and release
	 * the lock before our cmpxchgl instruction, causing us to fail 
	 * but resulting in the lock being held by our cpu.
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
	 * get it.  This routine may be called WITHOUT a critical section
	 * and with cpu interrupts enabled.
	 *
	 * To handle races in a sane fashion we predispose TD_MPCOUNT,
	 * which prevents us from losing the lock in a race if we already
	 * have it or happen to get it.  It also means that we might get
	 * the lock in an interrupt race before we have a chance to execute
	 * our cmpxchgl instruction, so we have to handle that case.
	 * Fortunately simply calling lwkt_switch() handles the situation
	 * for us and also 'blocks' us until the MP lock can be obtained.
	 */
NON_GPROF_ENTRY(get_mplock)
	movl	PCPU(cpuid),%ecx
	movl	PCPU(curthread),%edx
	incl	TD_MPCOUNT(%edx)	/* predispose */
	cmpl	%ecx,mp_lock
	jne	1f
	NON_GPROF_RET			/* success! */

	/*
	 * We don't already own the mp_lock, use cmpxchgl to try to get
	 * it.
	 */
1:
	movl	$-1,%eax
	lock cmpxchgl %ecx,mp_lock
	jnz	2f
#ifdef PARANOID_INVLTLB
	movl	%cr3,%eax; movl %eax,%cr3 /* YYY check and remove */
#endif
	NON_GPROF_RET			/* success */

	/*
	 * Failure, but we could end up owning mp_lock anyway due to
	 * an interrupt race.  lwkt_switch() will clean up the mess
	 * and 'block' until the mp_lock is obtained.
	 */
2:
	pause
	call	lwkt_switch
#ifdef INVARIANTS
	movl	PCPU(cpuid),%eax	/* failure */
	cmpl	%eax,mp_lock
	jne	4f
#endif
	NON_GPROF_RET
#ifdef INVARIANTS
4:
	cmpl	$0,panicstr		/* don't double panic */
	je	badmp_get2
	NON_GPROF_RET
#endif

	/*
	 * try_mplock() attempts to obtain the MP lock.  1 is returned on
	 * success, 0 on failure.  We do not have to be in a critical section
	 * and interrupts are almost certainly enabled.
	 *
	 * We must pre-dispose TD_MPCOUNT in order to deal with races in
	 * a reasonable way.
	 *
	 */
NON_GPROF_ENTRY(try_mplock)
	movl	PCPU(cpuid),%ecx
	movl	PCPU(curthread),%edx
	incl	TD_MPCOUNT(%edx)		/* pre-dispose for race */
	cmpl	%ecx,mp_lock
	je	1f				/* trivial success */
	movl	$-1,%eax
	lock cmpxchgl %ecx,mp_lock
	jnz	2f
	/*
	 * Success
	 */
#ifdef PARANOID_INVLTLB
	movl	%cr3,%eax; movl %eax,%cr3	/* YYY check and remove */
#endif
1:
	movl	$1,%eax				/* success (cmpxchgl good!) */
	NON_GPROF_RET

	/*
	 * The cmpxchgl failed but we might have raced.  Undo the mess by
	 * predispoing TD_MPCOUNT and then checking.  If TD_MPCOUNT is
	 * still non-zero we don't care what state the lock is in (since
	 * we obviously didn't own it above), just return failure even if
	 * we won the lock in an interrupt race.  If TD_MPCOUNT is zero
	 * make sure we don't own the lock in case we did win it in a race.
	 */
2:
	decl	TD_MPCOUNT(%edx)
	cmpl	$0,TD_MPCOUNT(%edx)
	jne	3f
	movl	PCPU(cpuid),%eax
	movl	$-1,%ecx
	lock cmpxchgl %ecx,mp_lock
3:
	subl	%eax,%eax
	NON_GPROF_RET
	
	/*
	 * rel_mplock() releases a previously obtained MP lock.
	 *
	 * In order to release the MP lock we pre-dispose TD_MPCOUNT for
	 * the release and basically repeat the release portion of try_mplock
	 * above.
	 */
NON_GPROF_ENTRY(rel_mplock)
	movl	PCPU(curthread),%edx
	movl	TD_MPCOUNT(%edx),%eax
#ifdef INVARIANTS
	cmpl	$0,%eax
	je	badmp_rel
#endif
	subl	$1,%eax
	movl	%eax,TD_MPCOUNT(%edx)
	cmpl	$0,%eax
	jne	3f
	movl	PCPU(cpuid),%eax
	movl	$-1,%ecx
	lock cmpxchgl %ecx,mp_lock
3:
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

	.data

bmpsw1:
	.asciz	"try/get_mplock(): already have lock! %d %p"

bmpsw1a:
	.asciz	"try/get_mplock(): failed on count or switch %d %p"

bmpsw2:
	.asciz	"rel_mplock(): mpcount already 0 @ %p %p %p %p %p %p %p %p!"

#endif

