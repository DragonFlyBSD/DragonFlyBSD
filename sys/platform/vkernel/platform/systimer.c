/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/platform/vkernel/platform/systimer.c,v 1.6 2007/01/09 18:26:59 dillon Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/systimer.h>
#include <sys/sysctl.h>
#include <sys/signal.h>
#include <sys/interrupt.h>
#include <sys/bus.h>
#include <sys/time.h>
#include <machine/cpu.h>
#include <machine/globaldata.h>

#include <sys/thread2.h>

#include <unistd.h>
#include <signal.h>

int disable_rtc_set;
SYSCTL_INT(_machdep, CPU_DISRTCSET, disable_rtc_set,
	   CTLFLAG_RW, &disable_rtc_set, 0, "");

int adjkerntz;
int wall_cmos_clock = 0;

/*
 * SYSTIMER IMPLEMENTATION
 */
static sysclock_t vkernel_timer_get_timecount(void);
static void vkernel_timer_construct(struct cputimer *timer, sysclock_t oclock);
static void cputimer_intr_hard(int signo);
static void cputimer_intr(void *dummy, void *frame);

static struct cputimer vkernel_cputimer = {
        SLIST_ENTRY_INITIALIZER,
        "VKERNEL",
        CPUTIMER_PRI_VKERNEL,
        CPUTIMER_VKERNEL,
        vkernel_timer_get_timecount,
        cputimer_default_fromhz,
        cputimer_default_fromus,
        vkernel_timer_construct,
        cputimer_default_destruct,
        1000000,			/* 1us granularity */
        0, 0, 0
};

/*
 * Initialize the systimer subsystem, called from MI code in early boot.
 */
void
cpu_initclocks(void)
{
	kprintf("initclocks\n");
	cputimer_register(&vkernel_cputimer);
	cputimer_select(&vkernel_cputimer, 0);
}

/*
 * Constructor to initialize timer->base and get an initial count.
 */
static void
vkernel_timer_construct(struct cputimer *timer, sysclock_t oclock)
{
	timer->base = 0;
	timer->base = oclock - vkernel_timer_get_timecount();
}

/*
 * Get the current counter, with 2's complement rollover.
 */
static sysclock_t
vkernel_timer_get_timecount(void)
{
	static sysclock_t vkernel_last_counter;
	struct timeval tv;
	sysclock_t counter;

	/* XXX NO PROTECTION FROM INTERRUPT */
	gettimeofday(&tv, NULL);
	counter = (sysclock_t)tv.tv_usec;
	if (counter < vkernel_last_counter)
		vkernel_cputimer.base += 1000000;
	vkernel_last_counter = counter;
	counter += vkernel_cputimer.base;
	return(counter);
}

/*
 * Configure the interrupt for our core systimer
 */
void
cputimer_intr_config(struct cputimer *timer)
{
	struct sigaction sa;

	kprintf("cputimer_intr_config\n");
	bzero(&sa, sizeof(sa));
	sa.sa_handler = cputimer_intr_hard;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGIO);
	sigaction(SIGALRM, &sa, NULL);
	register_int(0, cputimer_intr, NULL, "timer",
		     NULL, INTR_FAST|INTR_MPSAFE);
}

/*
 * Reload the interrupt for our core systimer.
 */
void
cputimer_intr_reload(sysclock_t reload)
{
	struct itimerval it;

	it.it_interval.tv_usec = 0;
	it.it_interval.tv_sec = 0;
	it.it_value.tv_usec = reload;
	it.it_value.tv_sec = 0;

	setitimer(ITIMER_REAL, &it, NULL);
}

/*
 * Clock interrupt (SIGALRM)
 *
 * Upon a clock interrupt, dispatch to the systimer subsystem
 *
 * XXX NO FRAME PROVIDED
 */
static
void
cputimer_intr_hard(int signo)
{
	struct mdglobaldata *gd = mdcpu;

	/*
	 * XXX check critical section hack
	 */
	if (curthread->td_pri >= TDPRI_CRIT) {
		atomic_set_int(&gd->gd_fpending, 1);
		atomic_set_int(&gd->mi.gd_reqflags, RQF_INTPEND);
	} else {
		crit_enter();
		cputimer_intr(NULL, NULL);
		crit_exit();
	}
}

static
void
cputimer_intr(void *dummy, void *frame)
{
	static sysclock_t sysclock_count;
	struct globaldata *gd = mycpu;
#ifdef SMP
        struct globaldata *gscan;
	int n;
#endif
	sysclock_count = sys_cputimer->count();
#ifdef SMP
	for (n = 0; n < ncpus; ++n) {
		gscan = globaldata_find(n);
		if (TAILQ_FIRST(&gscan->gd_systimerq) == NULL)
			continue;
		if (gscan != gd) {
			lwkt_send_ipiq3(gscan, (ipifunc3_t)systimer_intr,
					&sysclock_count, 0);
		} else {
			systimer_intr(&sysclock_count, 0, NULL);
		}
	}
#else
	if (TAILQ_FIRST(&gd->gd_systimerq) != NULL)
		systimer_intr(&sysclock_count, 0, NULL);
#endif
}

/*
 * Initialize the time of day register, based on the time base which is, e.g.
 * from a filesystem.
 */
void
inittodr(time_t base)
{
	struct timespec ts;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * 1000;
	set_timeofday(&ts);
}

/*
 * Write system time back to the RTC
 */
void
resettodr(void)
{
}

void
DELAY(int usec)
{
	usleep(usec);
}


