/*-
 * Copyright (c) 2006,2007 Konstantin Belousov
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
 * $FreeBSD$
 */

#include "linux_assym.h"		/* system definitions */
#include <machine/asmacros.h>		/* miscellaneous asm macros */
#include <machine/cputypes.h>
#include <machine/pmap.h>
#include <machine/specialreg.h>

#include "assym.s"

/*
 * A stack-based on-fault routine is used for more complex PCB_ONFAULT
 * situations (such as memcpy/bcopy/bzero).  In this case the on-fault
 * routine must be pushed on the stack.
 */
stack_onfault:
	ret

futex_fault_decx:
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx), %ecx
futex_fault:
	movl	$0,PCB_ONFAULT(%ecx)
	movl	$EFAULT,%eax
	ret

futex_fault_pop:
	addl	$4,%esp
	movl	$0,PCB_ONFAULT(%ecx)
	movl	$EFAULT,%eax
	ret

ENTRY(futex_xchgl)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	pushl	$futex_fault
	movl	$stack_onfault, PCB_ONFAULT(%ecx)
	movl	8(%esp),%eax
	movl	12(%esp),%edx
	cmpl    $VM_MAX_USER_ADDRESS-4,%edx
	ja     	futex_fault_pop
	xchgl	%eax,(%edx)
	movl	16(%esp),%edx
	movl	%eax,(%edx)
	xorl	%eax,%eax
	movl	%eax,PCB_ONFAULT(%ecx)
	ret

ENTRY(futex_addl)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	pushl	$futex_fault
	movl	$stack_onfault,PCB_ONFAULT(%ecx)
	movl	8(%esp),%eax
	movl	12(%esp),%edx
	cmpl    $VM_MAX_USER_ADDRESS-4,%edx
	ja     	futex_fault_pop
	lock
	xaddl	%eax,(%edx)
	movl	16(%esp),%edx
	movl	%eax,(%edx)
	xorl	%eax,%eax
	movl	%eax,PCB_ONFAULT(%ecx)
	ret

ENTRY(futex_orl)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	pushl	$futex_fault_decx
	movl	$stack_onfault,PCB_ONFAULT(%ecx)
	movl	12(%esp),%edx
	cmpl    $VM_MAX_USER_ADDRESS-4,%edx
	ja     	futex_fault_pop
	movl	(%edx),%eax
1:	movl	%eax,%ecx
	orl	8(%esp),%ecx
	lock
	cmpxchgl %ecx,(%edx)
	jnz	1b
futex_tail:
	movl	16(%esp),%edx
	movl	%eax,(%edx)
	xorl	%eax,%eax
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	movl	%eax,PCB_ONFAULT(%ecx)
	ret

ENTRY(futex_andl)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	pushl	$futex_fault_decx
	movl	$stack_onfault,PCB_ONFAULT(%ecx)
	movl	12(%esp),%edx
	cmpl    $VM_MAX_USER_ADDRESS-4,%edx
	ja     	futex_fault_pop
	movl	(%edx),%eax
1:	movl	%eax,%ecx
	andl	8(%esp),%ecx
	lock
	cmpxchgl %ecx,(%edx)
	jnz	1b
	jmp	futex_tail

ENTRY(futex_xorl)
	movl	PCPU(curthread),%ecx
	movl	TD_PCB(%ecx),%ecx
	pushl	$futex_fault_decx
	movl	$stack_onfault,PCB_ONFAULT(%ecx)
	movl	12(%esp),%edx
	cmpl    $VM_MAX_USER_ADDRESS-4,%edx
	ja     	futex_fault_pop
	movl	(%edx),%eax
1:	movl	%eax,%ecx
	xorl	8(%esp),%ecx
	lock
	cmpxchgl %ecx,(%edx)
	jnz	1b
	jmp	futex_tail
