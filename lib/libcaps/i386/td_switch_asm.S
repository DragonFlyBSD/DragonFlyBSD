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
 * $DragonFly: src/lib/libcaps/i386/td_switch_asm.S,v 1.1 2003/12/04 22:06:22 dillon Exp $
 */

#include "asmacros.h"
#include "asdefs.h"

	.text

	.globl	cpu_exit_switch
	.globl	cpu_kthread_start
	.globl	cpu_lwkt_switch
	.globl	cpu_lwkt_restore

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
cpu_exit_switch:
	/*
	 * Get us out of the vmspace
	 */
	movl	PCPU(curthread),%ebx
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
 * cpu_kthread_start() (current thread is %eax on entry) (one-time execution)
 *
 *	Run only on first function
 */
cpu_kthread_start:
	andl	$~TDF_RUNNING,TD_FLAGS(%ebx)
	orl	$TDF_RUNNING,TD_FLAGS(%eax)
	subl	$TDPRI_CRIT,TD_PRI(%eax)

	movl	$0,%eax
	movl	%eax,%ebx
	movl	%eax,%ecx
	movl	%eax,%edx
	movl	%eax,%esi
	movl	%eax,%edi
	movl	%eax,%ebp
	ret

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
cpu_lwkt_switch:
	movl	4(%esp),%eax
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	pushfl
	movl	PCPU(curthread),%ebx
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
cpu_lwkt_restore:
	andl	$~TDF_RUNNING,TD_FLAGS(%ebx)
	orl	$TDF_RUNNING,TD_FLAGS(%eax)
	popfl
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret

	.end
