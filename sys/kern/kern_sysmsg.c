/*
 * Copyright (c) 2003,2004 Matthew Dillon <dillon@backplane.com>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/kern/Attic/kern_sysmsg.c,v 1.1 2004/06/04 20:35:36 dillon Exp $
 */

/*
 * SYSMSG is our system call message encapsulation and handling subsystem.
 * System calls are now encapsulated in messages.  A system call can execute
 * synchronously or asynchronously.  If a system call wishes to run 
 * asynchronously it returns EASYNC and the process records the pending system
 * call message in p_sysmsgq.
 *
 * SYSMSGs work similarly to LWKT messages in that the originator can request
 * a synchronous or asynchronous operation in isolation from the actual system
 * call which can choose to run the system call synchronous or asynchronously
 * (independant of what was requested).  Like LWKT messages, the synchronous
 * path avoids all queueing operations and is almost as fast as making a 
 * direct procedure call.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/pioctl.h>
#include <sys/kernel.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/uio.h>
#include <sys/vmmeter.h>
#include <sys/malloc.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif
#include <sys/upcall.h>
#include <sys/sysproto.h>
#include <sys/sysunion.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>

#include <sys/msgport2.h>
#include <sys/thread2.h>

/*
 * Wait for a system call message to be returned.  If NULL is passed we
 * wait for the next ready sysmsg and return it.  We return NULL if there
 * are no pending sysmsgs queued.
 *
 * NOTE: proc must be curproc.
 */
struct sysmsg *
sysmsg_wait(struct proc *p, struct sysmsg *sysmsg, int nonblock)
{
	thread_t td = p->p_thread;

	/*
	 * Get the next finished system call or the specified system call,
	 * blocking until it is finished (if requested).
	 */
	if (sysmsg == NULL) {
		if (TAILQ_FIRST(&p->p_sysmsgq) == NULL)
			return(NULL);
		if (nonblock) {
			if ((sysmsg = lwkt_getport(&td->td_msgport)) == NULL)
				return(NULL);
		} else {
			sysmsg = lwkt_waitport(&td->td_msgport, NULL);
		}
	} else {
		if (nonblock && !lwkt_checkmsg(&sysmsg->lmsg))
			return(NULL);
		lwkt_waitport(&td->td_msgport, &sysmsg->lmsg);
	}

	/*
	 * sysmsg is not NULL here
	 */
	TAILQ_REMOVE(&p->p_sysmsgq, sysmsg, msgq);
	return(sysmsg);
}

/*
 * Wait for all pending asynchronous system calls to complete, aborting them
 * if requested (XXX).
 */
void
sysmsg_rundown(struct proc *p, int doabort)
{
	struct sysmsg *sysmsg;
	thread_t td = p->p_thread;
	globaldata_t gd = td->td_gd;

	while (TAILQ_FIRST(&p->p_sysmsgq) != NULL) {
		printf("WAITSYSMSG\n");
		sysmsg = sysmsg_wait(p, NULL, 0);
		printf("WAITSYSMSG %p\n", sysmsg);
		KKASSERT(sysmsg != NULL);
		/* XXX don't bother with pending copyouts */
		/* XXX we really should do pending copyouts */
		crit_enter_quick(td);
		sysmsg->lmsg.opaque.ms_sysunnext = gd->gd_freesysun;
		gd->gd_freesysun = (void *)sysmsg;
		crit_exit_quick(td);
	}
}

