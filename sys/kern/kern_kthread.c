/*
 * Copyright (c) 1999 Peter Wemm <peter@FreeBSD.org>
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
 * $FreeBSD: src/sys/kern/kern_kthread.c,v 1.5.2.3 2001/12/25 01:51:14 dillon Exp $
 * $DragonFly: src/sys/kern/kern_kthread.c,v 1.8 2003/06/30 19:50:31 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/ptrace.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/unistd.h>
#include <sys/wait.h>

#include <machine/stdarg.h>

/*
 * Start a kernel process.  This is called after a fork() call in
 * mi_startup() in the file kern/init_main.c.
 *
 * This function is used to start "internal" daemons and intended
 * to be called from SYSINIT().
 */
void
kproc_start(udata)
	const void *udata;
{
	const struct kproc_desc	*kp = udata;
	int error;

	error = kthread_create((void (*)(void *))kp->func, NULL,
		    kp->global_threadpp, kp->arg0);
	lwkt_setpri(*kp->global_threadpp, TDPRI_KERN_DAEMON);
	if (error)
		panic("kproc_start: %s: error %d", kp->arg0, error);
}

/*
 * Advise a kernel process to suspend (or resume) in its main loop.
 * Participation is voluntary.
 */
int
suspend_kproc(struct thread *td, int timo)
{
	if (td->td_proc == NULL) {
		td->td_flags |= TDF_STOPREQ;	/* request thread pause */
		wakeup(td);
		while (td->td_flags & TDF_STOPREQ) {
			int error = tsleep(td, PPAUSE, "suspkp", timo);
			if (error == EWOULDBLOCK)
				break;
		}
		td->td_flags &= ~TDF_STOPREQ;
		return(0);
	} else {
		return(EINVAL);	/* not a kernel thread */
	}
}

void
kproc_suspend_loop(void)
{
	struct thread *td = curthread;

	if (td->td_flags & TDF_STOPREQ) {
		td->td_flags &= ~TDF_STOPREQ;
		while ((td->td_flags & TDF_WAKEREQ) == 0) {
			wakeup(td);
			tsleep(td, PPAUSE, "kpsusp", 0);
		}
		td->td_flags &= ~TDF_WAKEREQ;
		wakeup(td);
	}
}

