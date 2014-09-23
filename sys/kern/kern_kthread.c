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

static struct lwkt_token kpsus_token = LWKT_TOKEN_INITIALIZER(kpsus_token);


/*
 * Create a new lightweight kernel thread.
 */
static int
_kthread_create(void (*func)(void *), void *arg,
    struct thread **tdp, int cpu, const char *fmt, __va_list ap)
{
    thread_t td;
    int flags = 0;

    if (bootverbose)
        atomic_set_int(&flags, TDF_VERBOSE);

    td = lwkt_alloc_thread(NULL, LWKT_THREAD_STACK, cpu, flags);
    if (tdp)
	*tdp = td;
    cpu_set_thread_handler(td, kthread_exit, func, arg);

    /*
     * Set up arg0 for 'ps' etc
     */
    kvsnprintf(td->td_comm, sizeof(td->td_comm), fmt, ap);

    td->td_ucred = crhold(proc0.p_ucred);

    /*
     * Schedule the thread to run
     */
    lwkt_schedule(td);

    return 0;
}

/*
 * Creates a lwkt. No CPU preference.
 */
int
kthread_create(void (*func)(void *), void *arg,
	       struct thread **tdp, const char *fmt, ...)
{
	__va_list ap;
	int ret;

	__va_start(ap, fmt);
	ret = _kthread_create(func, arg, tdp, -1, fmt, ap);
	__va_end(ap);

	return ret;
}

/*
 * Creates a lwkt and schedule it to run in a specific CPU.
 *
 */
int
kthread_create_cpu(void (*func)(void *), void *arg,
		   struct thread **tdp, int cpu, const char *fmt, ...)
{
	__va_list ap;
	int ret;

	__va_start(ap, fmt);
	ret = _kthread_create(func, arg, tdp, cpu, fmt, ap);
	__va_end(ap);

	return ret;
}

#if 0
/*
 * Same as kthread_create() but you can specify a custom stack size.
 */
int
kthread_create_stk(void (*func)(void *), void *arg,
		   struct thread **tdp, int stksize, const char *fmt, ...)
{
    thread_t td;
    __va_list ap;

    td = lwkt_alloc_thread(NULL, stksize, -1, TDF_VERBOSE);
    if (tdp)
	*tdp = td;
    cpu_set_thread_handler(td, kthread_exit, func, arg);

    __va_start(ap, fmt);
    kvsnprintf(td->td_comm, sizeof(td->td_comm), fmt, ap);
    __va_end(ap);

    lwkt_schedule(td);
    return 0;
}
#endif

/*
 * Destroy an LWKT thread.   Warning!  This function is not called when
 * a process exits, cpu_proc_exit() directly calls cpu_thread_exit() and
 * uses a different reaping mechanism.
 *
 * XXX duplicates lwkt_exit()
 */
void
kthread_exit(void)
{
    lwkt_exit();
}

/*
 * Start a kernel process.  This is called after a fork() call in
 * mi_startup() in the file kern/init_main.c.
 *
 * This function is used to start "internal" daemons and intended
 * to be called from SYSINIT().
 *
 * These threads are created MPSAFE.
 */
void
kproc_start(const void *udata)
{
	const struct kproc_desc	*kp = udata;
	int error;

	error = kthread_create((void (*)(void *))kp->func, NULL,
				kp->global_threadpp, "%s", kp->arg0);
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
		lwkt_gettoken(&kpsus_token);
		/* request thread pause */
		atomic_set_int(&td->td_mpflags, TDF_MP_STOPREQ);
		wakeup(td);
		while (td->td_mpflags & TDF_MP_STOPREQ) {
			int error = tsleep(td, 0, "suspkp", timo);
			if (error == EWOULDBLOCK)
				break;
		}
		atomic_clear_int(&td->td_mpflags, TDF_MP_STOPREQ);
		lwkt_reltoken(&kpsus_token);
		return(0);
	} else {
		return(EINVAL);	/* not a kernel thread */
	}
}

void
kproc_suspend_loop(void)
{
	struct thread *td = curthread;

	if (td->td_mpflags & TDF_MP_STOPREQ) {
		lwkt_gettoken(&kpsus_token);
		atomic_clear_int(&td->td_mpflags, TDF_MP_STOPREQ);
		while ((td->td_mpflags & TDF_MP_WAKEREQ) == 0) {
			wakeup(td);
			tsleep(td, 0, "kpsusp", 0);
		}
		atomic_clear_int(&td->td_mpflags, TDF_MP_WAKEREQ);
		wakeup(td);
		lwkt_reltoken(&kpsus_token);
	}
}
