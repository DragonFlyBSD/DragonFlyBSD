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
 * $DragonFly: src/sys/platform/pc64/amd64/swtch.s,v 1.1 2007/09/23 04:29:31 yanyh Exp $
 * $DragonFly: src/sys/platform/pc64/amd64/swtch.s,v 1.1 2007/09/23 04:29:31 yanyh Exp $
 */

#include "use_npx.h"

#include <sys/rtprio.h>

#include <machine/asmacros.h>
#include <machine/segments.h>

#include <machine/pmap.h>
#include <machine/lock.h>

#include "assym.s"

#if defined(SMP)
#define MPLOCKED        lock ;
#else
#define MPLOCKED
#endif

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
 *	Some instructions have been reordered to reduce pipeline stalls.
 *
 *	YYY disable interrupts once giant is removed.
 */
ENTRY(cpu_heavy_switch)

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
 * cpu_heavy_restore()	(current thread in %eax on entry)
 *
 *	Restore the thread after an LWKT switch.  This entry is normally
 *	called via the LWKT switch restore function, which was pulled 
 *	off the thread stack and jumped to.
 *
 *	This entry is only called if the thread was previously saved
 *	using cpu_heavy_switch() (the heavy weight process thread switcher),
 *	or when a new process is initially scheduled.  The first thing we
 *	do is clear the TDF_RUNNING bit in the old thread and set it in the
 *	new thread.
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

/*
 * savectx(pcb)
 *
 * Update pcb, saving current processor state.
 */
ENTRY(savectx)

/*
 * cpu_idle_restore()	(current thread in %eax on entry) (one-time execution)
 *
 *	Don't bother setting up any regs other then %ebp so backtraces
 *	don't die.  This restore function is used to bootstrap into the
 *	cpu_idle() LWKT only, after that cpu_lwkt_*() will be used for
 *	switching.
 *
 *	Clear TDF_RUNNING in old thread only after we've cleaned up %cr3.
 *
 *	If we are an AP we have to call ap_init() before jumping to
 *	cpu_idle().  ap_init() will synchronize with the BP and finish
 *	setting up various ncpu-dependant globaldata fields.  This may
 *	happen on UP as well as SMP if we happen to be simulating multiple
 *	cpus.
 */
ENTRY(cpu_idle_restore)

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
 */
ENTRY(cpu_kthread_restore)

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

/*
 * bootstrap_idle()
 *
 * Make AP become the idle loop.
 */
ENTRY(bootstrap_idle)
