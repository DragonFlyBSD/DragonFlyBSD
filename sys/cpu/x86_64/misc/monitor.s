/*
 * Copyright (c) 2010 The DragonFly Project. All rights reserved.
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Venkatesh Srinivas <me@endeavour.zapto.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asmacros.h>
#include <machine/cputypes.h>
#include <machine/pmap.h>
#include <machine/specialreg.h>

#include "assym.s"

	.text

/*
 * long cpu_mmw_spin(long *addr, long oldval) (%rdi, %rsi)
 *
 *	Spin on addr, waiting for it to no longer contain oldval; 
 *	return newval.
 */
ENTRY(cpu_mmw_spin)
	.align 4
1:
	movq	(%rdi),  %rax
	cmpq	%rsi, %rax
	pause
	je	1b
	ret

/*
 * long cpu_mmw_mwait(long *addr, long oldval)
 *
 * 	Spin on addr, waiting for it to no longer contain oldval;
 *	return newval. Use the MONITOR/MWAIT instructions to wait
 *	for a state-change on the address.
 *	
 *	WARN 1: We receive wakeup events for much larger windows
 *		than a single address; CPUID EAX = 0x05 reports the
 *		windows; on an Intel Atom they seem to cacheline-sized.
 *		Synchronization variables should probably be 
 *		cacheline-aligned to avoid false wakeups.
 *
 *	WARN 2: This routine can be racy; when we wake from MWAIT, we must
 *		load the contents of the address; in the meantime, it
 *		is possible that it was swapped to the prior (or some other)
 *		value; care must be used -- CMPXCHG for wakeup, for example.
 *
 *	WARN 3: Use this routine only when cpu_mi_features & CPU_MI_MONITOR
 */	
ENTRY(cpu_mmw_mwait)
	movq	%rdi, %rax
	xorq	%rdi, %rdi

	.align	4
1:
	xorq	%rcx, %rcx
	monitor
	movq	(%rax), %rcx
	cmpq	%rsi, %rcx
	jne	2f
	mwait
	jmp	1b

2:
	.align	4
	movq	%rcx, %rax
	ret
