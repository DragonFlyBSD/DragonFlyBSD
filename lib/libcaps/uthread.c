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
 * $DragonFly: src/lib/libcaps/uthread.c,v 1.1 2003/12/04 22:06:19 dillon Exp $
 */

/*
 * Each cpu in a system has its own self-contained light weight kernel
 * thread scheduler, which means that generally speaking we only need
 * to use a critical section to avoid problems.  Foreign thread 
 * scheduling is queued via (async) IPIs.
 *
 * NOTE: on UP machines smp_active is defined to be 0.  On SMP machines
 * smp_active is 0 prior to SMP activation, then it is 1.  The LWKT module
 * uses smp_active to optimize UP builds and to avoid sending IPIs during
 * early boot (primarily interrupt and network thread initialization).
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
    for (;;) {
        /*
         * if the idle thread and the main thread are the only remaining
	 * threads then switch to single threaded mode.  Accomplish this
	 * by switching to the main thread.  XXX
         */
	globaldata_t gd = mycpu;
        if (gd->gd_num_threads == 0) {	/* XXX not working yet */
            lwkt_schedule(&main_td);
        }
	lwkt_switch();
    }
}

/*
 * Userland override of lwkt_init_thread. The only difference is
 * the manipulation of gd->gd_num_threads;
 */
static void
lwkt_init_thread_remote(void *arg)
{ 
    thread_t td = arg;
     
    TAILQ_INSERT_TAIL(&td->td_gd->gd_tdallq, td, td_allq);
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
    if (smp_active == 0 || gd == mycpu) {
	crit_enter();
	TAILQ_INSERT_TAIL(&gd->gd_tdallq, td, td_allq);
        gd->gd_num_threads++;       /* Userland specific */
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

    if (td->td_flags & TDF_VERBOSE)
	printf("kthread %p %s has exited\n", td, td->td_comm);
    crit_enter();
    lwkt_deschedule_self();
    ++mycpu->gd_tdfreecount;
    --mycpu->gd_num_threads;        /* Userland specific */
    TAILQ_INSERT_TAIL(&mycpu->gd_tdfreeq, td, td_threadq);
    cpu_thread_exit();
}

/*
 * Userland override of lwkt_gdinit.  Called from mi_gdinit().
 */
void
lwkt_gdinit(struct globaldata *gd)
{
    /* Kernel Version of lwkt_gdinit() */
    int i;

    for (i = 0; i < sizeof(gd->gd_tdrunq)/sizeof(gd->gd_tdrunq[0]); ++i)
	TAILQ_INIT(&gd->gd_tdrunq[i]);
    gd->gd_runqmask = 0;
    gd->gd_curthread = &gd->gd_idlethread;
    TAILQ_INIT(&gd->gd_tdallq);

    /* Set up this cpu's idle thread */
    lwkt_init_thread(&gd->gd_idlethread, libcaps_alloc_stack(THREAD_STACK), 0, gd);
    cpu_set_thread_handler(&gd->gd_idlethread, lwkt_exit, lwkt_idleloop, NULL);

    /*
     * lwkt_init_thread added threads to gd->gd_tdallq and incrementented
     * gd->gd_num_threads accordingly.  Reset the count to zero here.
     * The reason gd_num_threads exists is so a check can be performed
     * to see if there are any non bookkeeping threads running on this
     * virtual cpu.  We have created some threads for bookkeeping here that
     * shouldn't be counted. Resetting the count to 0 allows the test
     * if(mycpu->gd_num_threads == 0) to correctly test if there are any
     * non-bookeeping threads running on this virtual cpu.
     */
    gd->gd_num_threads = 0;
}

/*
 * Start threading.
 */
void
lwkt_start_threading(thread_t td)
{
    lwkt_switch();
}
