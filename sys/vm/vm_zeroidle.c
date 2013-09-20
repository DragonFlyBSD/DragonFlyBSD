/*
 * (MPSAFE)
 *
 * Copyright (c) 1994 John Dyson
 * Copyright (c) 2001 Matt Dillon
 * Copyright (c) 2010 The DragonFly Project
 *
 * All Rights Reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Venkatesh Srinivas <me@endeavour.zapto.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: @(#)vm_machdep.c	7.3 (Berkeley) 5/13/91
 *	Utah $Hdr: vm_machdep.c	1.16.1.1 89/06/23$
 * from FreeBSD: .../i386/vm_machdep.c,v 1.165 2001/07/04 23:27:04 dillon
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/vmmeter.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/thread.h>
#include <sys/kthread.h>
#include <sys/unistd.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <cpu/lwbuf.h>

#include <sys/thread2.h>
#include <vm/vm_page2.h>

/*
 * Implement the pre-zeroed page mechanism.
 */
#define ZIDLE_LO(v)	((v) * 2 / 3)
#define ZIDLE_HI(v)	((v) * 4 / 5)

/* Number of bytes to zero between reschedule checks */
#define IDLEZERO_RUN	(64)

/* Maximum number of pages per second to zero */
#define NPAGES_RUN	(20000)

static int idlezero_enable = 1;
TUNABLE_INT("vm.idlezero_enable", &idlezero_enable);
SYSCTL_INT(_vm, OID_AUTO, idlezero_enable, CTLFLAG_RW, &idlezero_enable, 0,
	   "Allow the kernel to use idle CPU cycles to zero pages");
static int idlezero_rate = NPAGES_RUN;
SYSCTL_INT(_vm, OID_AUTO, idlezero_rate, CTLFLAG_RW, &idlezero_rate, 0,
	   "Maximum pages per second to zero");
static int idlezero_nocache = -1;
SYSCTL_INT(_vm, OID_AUTO, idlezero_nocache, CTLFLAG_RW, &idlezero_nocache, 0,
	   "Maximum pages per second to zero");

static int idlezero_count = 0;
SYSCTL_INT(_vm, OID_AUTO, idlezero_count, CTLFLAG_RD, &idlezero_count, 0,
	   "The number of physical pages prezeroed at idle time");

enum zeroidle_state {
	STATE_IDLE,
	STATE_GET_PAGE,
	STATE_ZERO_PAGE,
	STATE_RELEASE_PAGE
};

#define DEFAULT_SLEEP_TIME	(hz / 10)
#define LONG_SLEEP_TIME		(hz * 10)

static int zero_state;

/*
 * Attempt to maintain approximately 1/2 of our free pages in a
 * PG_ZERO'd state. Add some hysteresis to (attempt to) avoid
 * generally zeroing a page when the system is near steady-state.
 * Otherwise we might get 'flutter' during disk I/O / IPC or
 * fast sleeps. We also do not want to be continuously zeroing
 * pages because doing so may flush our L1 and L2 caches too much.
 *
 * Returns non-zero if pages should be zerod.
 */
static int
vm_page_zero_check(void)
{
	if (idlezero_enable == 0)
		return (0);
	if (zero_state == 0) {
		/*
		 * Wait for the count to fall to LO before starting
		 * to zero pages.
		 */
		if (vm_page_zero_count <= ZIDLE_LO(vmstats.v_free_count))
			zero_state = 1;
	} else {
		/*
		 * Once we are zeroing pages wait for the count to
		 * increase to HI before we stop zeroing pages.
		 */
		if (vm_page_zero_count >= ZIDLE_HI(vmstats.v_free_count))
			zero_state = 0;
	}
	return (zero_state);
}

/*
 * vm_pagezero should sleep for a longer time when idlezero is disabled or
 * when there is an excess of zeroed pages.
 */
static int
vm_page_zero_time(void)
{
	if (idlezero_enable == 0)
		return (LONG_SLEEP_TIME);
	if (vm_page_zero_count >= ZIDLE_HI(vmstats.v_free_count))
		return (LONG_SLEEP_TIME);
	return (DEFAULT_SLEEP_TIME);
}

/*
 * MPSAFE thread
 */
static void
vm_pagezero(void __unused *arg)
{
	vm_page_t m = NULL;
	struct lwbuf *lwb = NULL;
	struct lwbuf lwb_cache;
	enum zeroidle_state state = STATE_IDLE;
	char *pg = NULL;
	int npages = 0;
	int sleep_time;	
	int i = 0;

	/*
	 * Adjust thread parameters before entering our loop.  The thread
	 * is started with the MP lock held and with normal kernel thread
	 * priority.
	 *
	 * Also put us on the last cpu for now.
	 *
	 * For now leave the MP lock held, the VM routines cannot be called
	 * with it released until tokenization is finished.
	 */
	lwkt_setpri_self(TDPRI_IDLE_WORK);
	lwkt_setcpu_self(globaldata_find(ncpus - 1));
	sleep_time = DEFAULT_SLEEP_TIME;

	/*
	 * Loop forever
	 */
	for (;;) {
		switch(state) {
		case STATE_IDLE:
			/*
			 * Wait for work.
			 */
			tsleep(&zero_state, 0, "pgzero", sleep_time);
			if (vm_page_zero_check())
				npages = idlezero_rate / 10;
			sleep_time = vm_page_zero_time();
			if (npages)
				state = STATE_GET_PAGE;	/* Fallthrough */
			break;
		case STATE_GET_PAGE:
			/*
			 * Acquire page to zero
			 */
			if (--npages == 0) {
				state = STATE_IDLE;
			} else {
				m = vm_page_free_fromq_fast();
				if (m == NULL) {
					state = STATE_IDLE;
				} else {
					state = STATE_ZERO_PAGE;
					lwb = lwbuf_alloc(m, &lwb_cache);
					pg = (char *)lwbuf_kva(lwb);
					i = 0;
				}
			}
			break;
		case STATE_ZERO_PAGE:
			/*
			 * Zero-out the page
			 */
			while (i < PAGE_SIZE) {
				if (idlezero_nocache == 1)
					bzeront(&pg[i], IDLEZERO_RUN);
				else
					bzero(&pg[i], IDLEZERO_RUN);
				i += IDLEZERO_RUN;
				lwkt_yield();
			}
			state = STATE_RELEASE_PAGE;
			break;
		case STATE_RELEASE_PAGE:
			lwbuf_free(lwb);
			vm_page_flag_set(m, PG_ZERO);
			vm_page_free_toq(m);
			state = STATE_GET_PAGE;
			++idlezero_count;
			break;
		}
		lwkt_yield();
	}
}

static void
pagezero_start(void __unused *arg)
{
	int error;
	struct thread *td;

	if (idlezero_nocache < 0 && (cpu_mi_feature & CPU_MI_BZERONT))
		idlezero_nocache = 1;

	error = kthread_create(vm_pagezero, NULL, &td, "pagezero");
	if (error)
		panic("pagezero_start: error %d", error);
}

SYSINIT(pagezero, SI_SUB_KTHREAD_VM, SI_ORDER_ANY, pagezero_start, NULL);
