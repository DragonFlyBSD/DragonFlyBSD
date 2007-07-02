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
 * $DragonFly: src/sys/platform/vkernel/platform/systimer.c,v 1.14 2007/07/02 14:47:27 dillon Exp $
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
#include <machine/md_var.h>

#include <sys/thread2.h>

#include <unistd.h>
#include <signal.h>

static void cputimer_intr(void *dummy, struct intrframe *frame);

int disable_rtc_set;
SYSCTL_INT(_machdep, CPU_DISRTCSET, disable_rtc_set,
	   CTLFLAG_RW, &disable_rtc_set, 0, "");

int adjkerntz;
int wall_cmos_clock = 0;
static struct kqueue_info *kqueue_timer_info;

static int cputimer_mib[16];
static int cputimer_miblen;


/*
 * SYSTIMER IMPLEMENTATION
 */
static sysclock_t vkernel_timer_get_timecount(void);
static void vkernel_timer_construct(struct cputimer *timer, sysclock_t oclock);

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
cpu_initclocks(void *arg __unused)
{
	int len;
	kprintf("initclocks\n");
	len = sizeof(vkernel_cputimer.freq);
	if (sysctlbyname("kern.cputimer.freq", &vkernel_cputimer.freq, &len,
		         NULL, NULL) < 0) {
		panic("cpu_initclocks: can't get kern.cputimer.freq!");
	}
	len = sizeof(cputimer_mib)/sizeof(cputimer_mib[0]);
	if (sysctlnametomib("kern.cputimer.clock", cputimer_mib, &len) < 0)
		panic("cpu_initclocks: can't get kern.cputimer.clock!");
	cputimer_miblen = len;
	cputimer_register(&vkernel_cputimer);
	cputimer_select(&vkernel_cputimer, 0);
}
SYSINIT(clocksvk, SI_BOOT2_CLOCKREG, SI_ORDER_FIRST, cpu_initclocks, NULL)

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
 *
 * NOTE! MPSAFE, possibly no critical section
 */
static sysclock_t
vkernel_timer_get_timecount(void)
{
	struct mdglobaldata *gd = mdcpu;
	size_t len;
	sysclock_t counter;

	len = sizeof(counter);
	if (sysctl(cputimer_mib, cputimer_miblen, &counter, &len,
		   NULL, NULL) < 0) {
		panic("vkernel_timer_get_timecount: sysctl failed!");
	}
	return(counter);
}

/*
 * Configure the interrupt for our core systimer.  Use the kqueue timer
 * support functions.
 */
void
cputimer_intr_config(struct cputimer *timer)
{
	kqueue_timer_info = kqueue_add_timer(cputimer_intr, NULL);
}

/*
 * Reload the interrupt for our core systimer.  Because the caller's
 * reload calculation can be negatively indexed, we need a minimal
 * check to ensure that a reasonable reload value is selected. 
 */
void
cputimer_intr_reload(sysclock_t reload)
{
	if (kqueue_timer_info) {
		if ((int)reload < 1)
			reload = 1;
		kqueue_reload_timer(kqueue_timer_info, (reload + 999) / 1000);
	}
}

/*
 * clock interrupt.
 *
 * NOTE: frame is a struct intrframe pointer.
 */
static void
cputimer_intr(void *dummy, struct intrframe *frame)
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
			systimer_intr(&sysclock_count, 0, frame);
		}
	}
#else
	if (TAILQ_FIRST(&gd->gd_systimerq) != NULL)
		systimer_intr(&sysclock_count, 0, frame);
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

void
DRIVERSLEEP(int usec)
{
        if (mycpu->gd_intr_nesting_level)
		DELAY(usec);
	else if (1000000 / usec >= hz)
		tsleep(DRIVERSLEEP, 0, "DELAY", 1000000 / usec / hz + 1);
	else
		usleep(usec);
}

