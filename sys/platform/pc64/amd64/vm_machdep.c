/*-
 * Copyright (c) 1982, 1986 The Regents of the University of California.
 * Copyright (c) 1989, 1990 William Jolitz
 * Copyright (c) 1994 John Dyson
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department, and William Jolitz.
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
 *	from: @(#)vm_machdep.c	7.3 (Berkeley) 5/13/91
 *	Utah $Hdr: vm_machdep.c 1.16.1.1 89/06/23$
 * $FreeBSD: src/sys/i386/i386/vm_machdep.c,v 1.132.2.9 2003/01/25 19:02:23 dillon Exp $
 * $DragonFly: src/sys/platform/pc64/amd64/vm_machdep.c,v 1.1 2007/09/23 04:29:31 yanyh Exp $
 * $DragonFly: src/sys/platform/pc64/amd64/vm_machdep.c,v 1.1 2007/09/23 04:29:31 yanyh Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/interrupt.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/smp.h>
#include <machine/pcb_ext.h>
#include <machine/pcb.h>
#include <machine/segments.h>
#include <machine/globaldata.h>	/* npxthread */

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <sys/thread2.h>

char machine[] = MACHINE;

char cpu_vendor[] = "DragonFly";	/* XXX */
u_int cpu_id = 0x80000000;		/* XXX */

/*
 * Finish a fork operation, with lwp lp2 nearly set up.
 * Copy and update the pcb, set up the stack so that the child
 * ready to run and return to user mode.
 */
void
cpu_fork(struct lwp *lp1, struct lwp *lp2, int flags)
{
}

/*
 * Prepare new lwp to return to the address specified in params.
 */
int
cpu_prepare_lwp(struct lwp *lp, struct lwp_params *params)
{
	return (0);
}

/*
 * Intercept the return address from a freshly forked process that has NOT
 * been scheduled yet.
 *
 * This is needed to make kernel threads stay in kernel mode.
 */
void
cpu_set_fork_handler(struct lwp *lp, void (*func)(void *, struct trapframe *),
		     void *arg)
{
}

void
cpu_set_thread_handler(thread_t td, void (*rfunc)(void), void *func, void *arg)
{
}

void
cpu_lwp_exit(void)
{
}

/*
 * Terminate the current thread.  The caller must have already acquired
 * the thread's rwlock and placed it on a reap list or otherwise notified
 * a reaper of its existance.  We set a special assembly switch function which
 * releases td_rwlock after it has cleaned up the MMU state and switched
 * out the stack.
 *
 * Must be caller from a critical section and with the thread descheduled.
 */
void
cpu_thread_exit(void)
{
}

/*
 * Process Reaper.  Called after the caller has acquired the thread's
 * rwlock and removed it from the reap list.
 */
void
cpu_proc_wait(struct proc *p)
{
}

int
kvm_access_check(vm_offset_t saddr, vm_offset_t eaddr, int prot)
{
	return 0;
}

