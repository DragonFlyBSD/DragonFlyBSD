/*
 * Copyright (c) 2003 Galen Sampson <galen_sampson@yahoo.com>
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
 * $DragonFly: src/lib/libcaps/uthread.c,v 1.2 2003/12/07 04:21:52 dillon Exp $
 */

/*
 * Each cpu in a system has its own self-contained light weight kernel
 * thread scheduler, which means that generally speaking we only need
 * to use a critical section to avoid problems.  Foreign thread 
 * scheduling is queued via (async) IPIs.
 */

#include "defs.h"

void cpu_lwkt_switch(thread_t);

/*
 * system message port for the system call interface
 */
lwkt_port_t		sysport;

static void
lwkt_idleloop(void *dummy)
{
    globaldata_t gd = mycpu;

    DBPRINTF(("idlestart cpu %d pri %d (should be < 32) mpcount %d (should be 0)\n",
	gd->gd_cpuid, curthread->td_pri, curthread->td_mpcount));

    gd->gd_pid = getpid();

    for (;;) {
	/*
	 * If only our 'main' thread is left, schedule it.
	 */
        if (gd->gd_num_threads == gd->gd_sys_threads) {
	    int i;
	    globaldata_t tgd;

	    for (i = 0; i < ncpus; ++i) {
		tgd = globaldata_find(i);
		if (tgd->gd_num_threads != tgd->gd_sys_threads)
		    break;
	    }
	    if (i == ncpus && (main_td.td_flags & TDF_RUNQ) == 0)
		lwkt_schedule(&main_td);
        }

	/*
	 * Wait for an interrupt, aka wait for a signal or an upcall to
	 * occur, then switch away.
	 */
	crit_enter();
	if (gd->gd_runqmask || (curthread->td_flags & TDF_IDLE_NOHLT)) {
	    curthread->td_flags &= ~TDF_IDLE_NOHLT;
	} else {
	    printf("cpu %d halting\n", gd->gd_cpuid);
	    cpu_halt();
	    printf("cpu %d resuming\n", gd->gd_cpuid);
	}
	crit_exit();
	lwkt_switch();
    }
}

/*
 * Userland override of lwkt_init_thread. The only difference is
 * the manipulation of gd->gd_num_threads.
 */
static void
lwkt_init_thread_remote(void *arg)
{ 
    thread_t td = arg;
    globaldata_t gd = td->td_gd;

    printf("init_thread_remote td %p on cpu %d\n", td, gd->gd_cpuid);
     
    TAILQ_INSERT_TAIL(&gd->gd_tdallq, td, td_allq);
    ++gd->gd_num_threads;
    if (td->td_flags & TDF_SYSTHREAD)
	++gd->gd_sys_threads;
}

void
lwkt_init_thread(thread_t td, void *stack, int flags, struct globaldata *gd)
{
    bzero(td, sizeof(struct thread));
    td->td_kstack = stack;
    td->td_flags |= flags;
    td->td_gd = gd;
    td->td_pri = TDPRI_KERN_DAEMON + TDPRI_CRIT;
    lwkt_initport(&td->td_msgport, td);
    cpu_init_thread(td);
    if (td == &gd->gd_idlethread) {
	TAILQ_INSERT_TAIL(&gd->gd_tdallq, td, td_allq);
	/* idle thread is not counted in gd_num_threads */
    } else if (gd == mycpu) {
	crit_enter();
	TAILQ_INSERT_TAIL(&gd->gd_tdallq, td, td_allq);
        ++gd->gd_num_threads;
	if (td->td_flags & TDF_SYSTHREAD)
	    ++gd->gd_sys_threads;
	crit_exit();
    } else {
	lwkt_send_ipiq(gd->gd_cpuid, lwkt_init_thread_remote, td);
    }
}

/*
 * Userland override of lwkt_exit. The only difference is
 * the manipulation of gd->gd_num_threads;
 */
void
lwkt_exit(void)
{
    thread_t td = curthread;
    globaldata_t gd = mycpu;

    if (td->td_flags & TDF_VERBOSE)
	printf("kthread %p %s has exited\n", td, td->td_comm);
    crit_enter();
    lwkt_deschedule_self();
    ++gd->gd_tdfreecount;
    if (td->td_flags & TDF_SYSTHREAD)
	--gd->gd_sys_threads;
    --gd->gd_num_threads;
    TAILQ_INSERT_TAIL(&gd->gd_tdfreeq, td, td_threadq);
    cpu_thread_exit();
}

/*
 * Userland override of lwkt_gdinit.  Called from mi_gdinit().  Note that
 * critical sections do not work until lwkt_init_thread() is called.  The
 * idle thread will be left in a critical section.
 */
void
lwkt_gdinit(struct globaldata *gd)
{
    int i;

    for (i = 0; i < sizeof(gd->gd_tdrunq)/sizeof(gd->gd_tdrunq[0]); ++i)
	TAILQ_INIT(&gd->gd_tdrunq[i]);
    gd->gd_runqmask = 0;
    gd->gd_curthread = &gd->gd_idlethread;
    TAILQ_INIT(&gd->gd_tdallq);

    /* Set up this cpu's idle thread */
    lwkt_init_thread(&gd->gd_idlethread, libcaps_alloc_stack(THREAD_STACK), 0, gd);
    cpu_set_thread_handler(&gd->gd_idlethread, lwkt_exit, lwkt_idleloop, NULL);
}

/*
 * Start threading.
 */
void
lwkt_start_threading(thread_t td)
{
    lwkt_switch();
}

