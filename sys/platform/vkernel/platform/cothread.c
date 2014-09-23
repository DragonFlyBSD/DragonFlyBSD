/*
 * Copyright (c) 2008-2010 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/platform/vkernel/platform/cothread.c,v 1.3 2008/05/07 17:19:47 dillon Exp $
 */
/*
 * Provides the vkernel with an asynchronous I/O mechanism using pthreads
 * which operates outside the cpu abstraction.  Cothreads are intended to
 * operate like DMA engines and may ONLY make libc and cothread_*() calls.
 * The cothread may NOT call into the vkernel since abstractions like
 * 'mycpu' do not exist for it. 
 */

#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/memrange.h>
#include <sys/tls.h>
#include <sys/types.h>
#include <sys/bus.h>

#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/globaldata.h>
#include <machine/md_var.h>
#include <machine/pmap.h>
#include <machine/smp.h>
#include <machine/tls.h>
#include <machine/cothread.h>

#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>

static void cothread_thread(void *arg);

/*
 * Create a co-processor thread for a virtual kernel.  This thread operates
 * outside of the virtual kernel cpu abstraction and may only make direct
 * cothread and libc calls.
 */
cothread_t
cothread_create(void (*thr_func)(cothread_t cotd),
		void (*thr_intr)(cothread_t cotd),
		void *arg, const char *name)
{
	cothread_t cotd;

	cotd = kmalloc(sizeof(*cotd), M_DEVBUF, M_WAITOK|M_ZERO);
	cotd->thr_intr = thr_intr;
	cotd->thr_func = thr_func;
	cotd->arg = arg;
	crit_enter();
	pthread_mutex_init(&cotd->mutex, NULL);
	pthread_cond_init(&cotd->cond, NULL);
	crit_exit();

	cotd->pintr = pthread_self();

	cotd->intr_id = register_int_virtual(1, (void *)thr_intr, cotd, name,
	    NULL, INTR_MPSAFE);

	/*
	 * The vkernel's cpu_disable_intr() masks signals.  We don't want
	 * our coprocessor thread taking any unix signals :-)
	 */
	crit_enter();
	cpu_mask_all_signals();
	pthread_create(&cotd->pthr, NULL, (void *)cothread_thread, cotd);
	cpu_unmask_all_signals();
	crit_exit();
	return(cotd);
}

/*
 * Wait for the target thread to terminate and then destroy the cothread
 * structure.
 */
void
cothread_delete(cothread_t *cotdp)
{
	cothread_t cotd;

	if ((cotd = *cotdp) != NULL) {
		unregister_int_virtual(cotd->intr_id);
		crit_enter();
		pthread_join(cotd->pthr, NULL);
		crit_exit();
		kfree(cotd, M_DEVBUF);
		*cotdp = NULL;
	}
}

static void
cothread_thread(void *arg)
{
	cothread_t cotd = arg;
	int dummy = 0;

	cpu_mask_all_signals(); /* XXX remove me? should already be masked */
	/*
	 * %fs (aka mycpu) is illegal in cothreads.   Note that %fs is used
	 * by pthreads.
	 */
	tls_set_fs(&dummy, sizeof(dummy));
	cotd->thr_func(cotd);
}

/*
 * Called by the cothread to generate an interrupt back to the vkernel.
 */
void
cothread_intr(cothread_t cotd)
{
	pthread_kill(cotd->pintr, SIGIO);
}

/*
 * Called by the vkernel to wakeup a cothread.
 * The cothread must be locked.
 */
void
cothread_signal(cothread_t cotd)
{
	pthread_cond_signal(&cotd->cond);
}

/*
 * Called by the cothread to wait for the vkernel to call cothread_signal().
 * The cothread must be locked.
 */
void
cothread_wait(cothread_t cotd)
{
	pthread_cond_wait(&cotd->cond, &cotd->mutex);
}

/*
 * Typically called by kernel thread or cothread
 *
 * These must be a matched pair.  We will acquire a critical
 * section in cothread_lock() and release it in cothread_unlock().
 *
 * We do this to simplify cothread operation to prevent an
 * interrupt (e.g. vkd_io_intr()) from preempting a vkd_strategy()
 * call and creating a recursion in the pthread.
 */
void
cothread_lock(cothread_t cotd, int is_cotd)
{
	if (is_cotd) {
		pthread_mutex_lock(&cotd->mutex);
	} else {
		crit_enter_id("cothread");
		pthread_mutex_lock(&cotd->mutex);
	}
}

void
cothread_unlock(cothread_t cotd, int is_cotd)
{
	if (is_cotd) {
		pthread_mutex_unlock(&cotd->mutex);
	} else {
		pthread_mutex_unlock(&cotd->mutex);
		crit_exit_id("cothread");
	}
}
