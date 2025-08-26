/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2008-2021 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 *	from: @(#)clock.c	7.2 (Berkeley) 5/12/91
 * $FreeBSD: src/sys/i386/isa/clock.c,v 1.149.2.6 2002/11/02 04:41:50 iwasaki Exp $
 */

/*
 * Routines to handle clock hardware.
 */

/*
 * inittodr, settodr and support routines written
 * by Christoph Robitschko <chmr@edvz.tu-graz.ac.at>
 *
 * reintroduced and updated by Chris Stenton <chris@gnome.co.uk> 8/10/94
 */

#if 0
#include "opt_clock.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/sysctl.h>
#include <sys/cons.h>
#include <sys/kbio.h>
#include <sys/systimer.h>
#include <sys/globaldata.h>
#include <sys/machintr.h>
#include <sys/interrupt.h>

#include <sys/thread2.h>

#include <machine/clock.h>
#include <machine/cputypes.h>
#include <machine/frame.h>
#include <machine/ipl.h>
#include <machine/limits.h>
#include <machine/md_var.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/smp.h>
#include <machine/specialreg.h>
#include <machine/intr_machdep.h>

#include <machine_base/apic/ioapic.h>
#include <machine_base/apic/ioapic_abi.h>
#include <machine_base/icu/icu.h>
#include <bus/isa/isa.h>
#include <bus/isa/rtc.h>
#include <machine_base/isa/timerreg.h>

SET_DECLARE(timecounter_init_set, const timecounter_init_t);
TIMECOUNTER_INIT(placeholder, NULL);

static void i8254_restore(void);
static void resettodr_on_shutdown(void *arg __unused);

/*
 * 32-bit time_t's can't reach leap years before 1904 or after 2036, so we
 * can use a simple formula for leap years.
 */
#define	LEAPYEAR(y) ((u_int)(y) % 4 == 0)
#define DAYSPERYEAR   (31+28+31+30+31+30+31+31+30+31+30+31)

#ifndef TIMER_FREQ
#define TIMER_FREQ   1193182
#endif

static uint8_t i8254_walltimer_sel;
static uint16_t i8254_walltimer_cntr;
static int timer0_running;

int	adjkerntz;		/* local offset from GMT in seconds */
int	disable_rtc_set;	/* disable resettodr() if != 0 */
int	tsc_present;
int	tsc_invariant;
int	tsc_mpsync;
int	wall_cmos_clock;	/* wall CMOS clock assumed if != 0 */
tsc_uclock_t tsc_frequency;
tsc_uclock_t tsc_oneus_approx;	/* always at least 1, approx only */

enum tstate { RELEASED, ACQUIRED };
static enum tstate timer0_state;
static enum tstate timer1_state;
static enum tstate timer2_state;

int	i8254_cputimer_disable;	/* No need to initialize i8254 cputimer. */

static	int	beeping = 0;
static	const u_char daysinmonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
static	u_char	rtc_statusa = RTCSA_DIVIDER | RTCSA_NOPROF;
static	u_char	rtc_statusb = RTCSB_24HR | RTCSB_PINTR;
static  int	rtc_loaded;

static	sysclock_t i8254_cputimer_div;

static int i8254_nointr;
static int i8254_intr_disable = 1;
TUNABLE_INT("hw.i8254.intr_disable", &i8254_intr_disable);

static int calibrate_timers_with_rtc = 0;
TUNABLE_INT("hw.calibrate_timers_with_rtc", &calibrate_timers_with_rtc);

static int calibrate_tsc_fast = 1;
TUNABLE_INT("hw.calibrate_tsc_fast", &calibrate_tsc_fast);

static int calibrate_test;
TUNABLE_INT("hw.tsc_calibrate_test", &calibrate_test);

static struct callout sysbeepstop_ch;

static sysclock_t i8254_cputimer_count(void);
static void i8254_cputimer_construct(struct cputimer *cputimer, sysclock_t last);
static void i8254_cputimer_destruct(struct cputimer *cputimer);

static struct cputimer	i8254_cputimer = {
    .next		= SLIST_ENTRY_INITIALIZER,
    .name		= "i8254",
    .pri		= CPUTIMER_PRI_8254,
    .type		= 0,	/* determined later */
    .count		= i8254_cputimer_count,
    .fromhz		= cputimer_default_fromhz,
    .fromus		= cputimer_default_fromus,
    .construct		= i8254_cputimer_construct,
    .destruct		= i8254_cputimer_destruct,
    .freq		= TIMER_FREQ
};

static void i8254_intr_reload(struct cputimer_intr *, sysclock_t);
static void i8254_intr_config(struct cputimer_intr *, const struct cputimer *);
static void i8254_intr_initclock(struct cputimer_intr *, boolean_t);

static struct cputimer_intr i8254_cputimer_intr = {
    .freq = TIMER_FREQ,
    .reload = i8254_intr_reload,
    .enable = cputimer_intr_default_enable,
    .config = i8254_intr_config,
    .restart = cputimer_intr_default_restart,
    .pmfixup = cputimer_intr_default_pmfixup,
    .initclock = i8254_intr_initclock,
    .pcpuhand = NULL,
    .next = SLIST_ENTRY_INITIALIZER,
    .name = "i8254",
    .type = CPUTIMER_INTR_8254,
    .prio = CPUTIMER_INTR_PRIO_8254,
    .caps = CPUTIMER_INTR_CAP_PS,
    .priv = NULL
};

/*
 * Use this to lwkt_switch() when the scheduler clock is not
 * yet running, otherwise lwkt_switch() won't do anything.
 * XXX needs cleaning up in lwkt_thread.c
 */
static void
lwkt_force_switch(void)
{
	crit_enter();
	lwkt_schedulerclock(curthread);
	crit_exit();
	lwkt_switch();
}

/*
 * timer0 clock interrupt.  Timer0 is in one-shot mode and has stopped
 * counting as of this interrupt.  We use timer1 in free-running mode (not
 * generating any interrupts) as our main counter.  Each cpu has timeouts
 * pending.
 *
 * This code is INTR_MPSAFE and may be called without the BGL held.
 */
static void
clkintr(void *dummy, void *frame_arg)
{
	static sysclock_t sysclock_count;	/* NOTE! Must be static */
	struct globaldata *gd = mycpu;
	struct globaldata *gscan;
	int n;

	/*
	 * SWSTROBE mode is a one-shot, the timer is no longer running
	 */
	timer0_running = 0;

	/*
	 * XXX the dispatcher needs work.  right now we call systimer_intr()
	 * directly or via IPI for any cpu with systimers queued, which is
	 * usually *ALL* of them.  We need to use the LAPIC timer for this.
	 */
	sysclock_count = sys_cputimer->count();
	for (n = 0; n < ncpus; ++n) {
	    gscan = globaldata_find(n);
	    if (TAILQ_FIRST(&gscan->gd_systimerq) == NULL)
		continue;
	    if (gscan != gd) {
		lwkt_send_ipiq3(gscan, (ipifunc3_t)systimer_intr,
				&sysclock_count, 1);
	    } else {
		systimer_intr(&sysclock_count, 0, frame_arg);
	    }
	}
}


/*
 * NOTE! not MP safe.
 */
int
acquire_timer2(int mode)
{
	if (timer2_state != RELEASED)
		return (-1);
	timer2_state = ACQUIRED;

	/*
	 * This access to the timer registers is as atomic as possible
	 * because it is a single instruction.  We could do better if we
	 * knew the rate.
	 */
	outb(TIMER_MODE, TIMER_SEL2 | (mode & 0x3f));
	return (0);
}

int
release_timer2(void)
{
	if (timer2_state != ACQUIRED)
		return (-1);
	outb(TIMER_MODE, TIMER_SEL2 | TIMER_SQWAVE | TIMER_16BIT);
	timer2_state = RELEASED;
	return (0);
}

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>

DB_SHOW_COMMAND(rtc, rtc)
{
	kprintf("%02x/%02x/%02x %02x:%02x:%02x, A = %02x, B = %02x, C = %02x\n",
	       rtcin(RTC_YEAR), rtcin(RTC_MONTH), rtcin(RTC_DAY),
	       rtcin(RTC_HRS), rtcin(RTC_MIN), rtcin(RTC_SEC),
	       rtcin(RTC_STATUSA), rtcin(RTC_STATUSB), rtcin(RTC_INTR));
}
#endif /* DDB */

/*
 * Return the current cpu timer count.
 */
static
sysclock_t
i8254_cputimer_count(void)
{
	static uint16_t cputimer_last;
	uint16_t count;
	sysclock_t ret;

	clock_lock();
	outb(TIMER_MODE, i8254_walltimer_sel | TIMER_LATCH);
	count = (uint8_t)inb(i8254_walltimer_cntr);	/* get countdown */
	count |= ((uint8_t)inb(i8254_walltimer_cntr) << 8);
	count = -count;					/* -> countup */
	if (count < cputimer_last)			/* rollover */
		i8254_cputimer.base += 0x00010000U;
	ret = i8254_cputimer.base | count;
	cputimer_last = count;
	clock_unlock();

	return(ret);
}

/*
 * This function is called whenever the system timebase changes, allowing
 * us to calculate what is needed to convert a system timebase tick
 * into an 8254 tick for the interrupt timer.  If we can convert to a
 * simple shift, multiplication, or division, we do so.  Otherwise 64
 * bit arithmetic is required every time the interrupt timer is reloaded.
 */
static void
i8254_intr_config(struct cputimer_intr *cti, const struct cputimer *timer)
{
    sysclock_t freq;
    sysclock_t div;

    /*
     * Will a simple divide do the trick?
     */
    div = (timer->freq + (cti->freq / 2)) / cti->freq;
    freq = cti->freq * div;

    if (freq >= timer->freq - 1 && freq <= timer->freq + 1)
	i8254_cputimer_div = div;
    else
	i8254_cputimer_div = 0;
}

/*
 * Reload for the next timeout.  It is possible for the reload value
 * to be 0 or negative, indicating that an immediate timer interrupt
 * is desired.  For now make the minimum 2 ticks.
 *
 * We may have to convert from the system timebase to the 8254 timebase.
 */
static void
i8254_intr_reload(struct cputimer_intr *cti, sysclock_t reload)
{
    uint16_t count;

    if ((ssysclock_t)reload < 0)
	    reload = 1;
    if (i8254_cputimer_div)
	reload /= i8254_cputimer_div;
    else
	reload = muldivu64(reload, cti->freq, sys_cputimer->freq);

    if (reload < 2)
	reload = 2;		/* minimum count */
    if (reload > 0xFFFF)
	reload = 0xFFFF;	/* almost full count (0 is full count) */

    clock_lock();
    if (timer0_running) {
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);	/* count-down timer */
	count = (uint8_t)inb(TIMER_CNTR0);		/* lsb */
	count |= ((uint8_t)inb(TIMER_CNTR0) << 8);	/* msb */
	if (reload < count) {
	    outb(TIMER_MODE, TIMER_SEL0 | TIMER_SWSTROBE | TIMER_16BIT);
	    outb(TIMER_CNTR0, (uint8_t)reload); 	/* lsb */
	    outb(TIMER_CNTR0, (uint8_t)(reload >> 8));	/* msb */
	}
    } else {
	timer0_running = 1;
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_SWSTROBE | TIMER_16BIT);
	outb(TIMER_CNTR0, (uint8_t)reload); 		/* lsb */
	outb(TIMER_CNTR0, (uint8_t)(reload >> 8));	/* msb */
    }
    clock_unlock();
}

/*
 * DELAY(usec)	     - Spin for the specified number of microseconds.
 * DRIVERSLEEP(usec) - Spin for the specified number of microseconds,
 *		       but do a thread switch in the loop
 *
 * Relies on timer 1 counting down from (cputimer_freq / hz)
 * Note: timer had better have been programmed before this is first used!
 */
static void
DODELAY(int n, int doswitch)
{
	ssysclock_t delta, ticks_left;
	sysclock_t prev_tick, tick;

#ifdef DELAYDEBUG
	int getit_calls = 1;
	int n1;
	static int state = 0;

	if (state == 0) {
		state = 1;
		for (n1 = 1; n1 <= 10000000; n1 *= 10)
			DELAY(n1);
		state = 2;
	}
	if (state == 1)
		kprintf("DELAY(%d)...", n);
#endif
	/*
	 * Guard against the timer being uninitialized if we are called
	 * early for console i/o.
	 */
	if (timer0_state == RELEASED && i8254_cputimer_disable == 0)
		i8254_restore();

	/*
	 * Read the counter first, so that the rest of the setup overhead is
	 * counted.  Then calculate the number of hardware timer ticks
	 * required, rounding up to be sure we delay at least the requested
	 * number of microseconds.
	 */
	prev_tick = sys_cputimer->count();
	ticks_left = muldivu64(n, sys_cputimer->freq + 999999, 1000000);

	/*
	 * Loop until done.
	 */
	while (ticks_left > 0) {
		tick = sys_cputimer->count();
#ifdef DELAYDEBUG
		++getit_calls;
#endif
		delta = tick - prev_tick;
		prev_tick = tick;
		if (delta < 0)
			delta = 0;
		ticks_left -= delta;
		if (doswitch && ticks_left > 0)
			lwkt_switch();
		cpu_pause();
	}
#ifdef DELAYDEBUG
	if (state == 1)
		kprintf(" %d calls to getit() at %d usec each\n",
			getit_calls, (n + 5) / getit_calls);
#endif
}

/*
 * DELAY() never switches.
 */
void
DELAY(int n)
{
	DODELAY(n, 0);
}

/*
 * Returns non-zero if the specified time period has elapsed.  Call
 * first with last_clock set to 0.
 */
int
CHECKTIMEOUT(TOTALDELAY *tdd)
{
	sysclock_t delta;
	int us;

	if (tdd->started == 0) {
		if (timer0_state == RELEASED && i8254_cputimer_disable == 0)
			i8254_restore();
		tdd->last_clock = sys_cputimer->count();
		tdd->started = 1;
		return(0);
	}
	delta = sys_cputimer->count() - tdd->last_clock;
	us = muldivu64(delta, 1000000, sys_cputimer->freq);
	tdd->last_clock += muldivu64(us, sys_cputimer->freq, 1000000);
	tdd->us -= us;

	return (tdd->us < 0);
}


/*
 * DRIVERSLEEP() does not switch if called with a spinlock held or
 * from a hard interrupt.
 */
void
DRIVERSLEEP(int usec)
{
	globaldata_t gd = mycpu;

	if (gd->gd_intr_nesting_level || gd->gd_spinlocks) {
		DODELAY(usec, 0);
	} else {
		DODELAY(usec, 1);
	}
}

static void
sysbeepstop(void *chan)
{
	outb(IO_PPI, inb(IO_PPI)&0xFC);	/* disable counter2 output to speaker */
	beeping = 0;
	release_timer2();
}

int
sysbeep(int pitch, int period)
{
	if (acquire_timer2(TIMER_SQWAVE|TIMER_16BIT))
		return(-1);
	if (sysbeep_enable == 0)
		return(-1);
	/*
	 * Nobody else is using timer2, we do not need the clock lock
	 */
	outb(TIMER_CNTR2, pitch);
	outb(TIMER_CNTR2, (pitch>>8));
	if (!beeping) {
		/* enable counter2 output to speaker */
		outb(IO_PPI, inb(IO_PPI) | 3);
		beeping = period;
		callout_reset(&sysbeepstop_ch, period, sysbeepstop, NULL);
	}
	return (0);
}

/*
 * RTC support routines
 */

int
rtcin(int reg)
{
	u_char val;

	crit_enter();
	outb(IO_RTC, reg);
	inb(0x84);
	val = inb(IO_RTC + 1);
	inb(0x84);
	crit_exit();
	return (val);
}

static __inline void
writertc(u_char reg, u_char val)
{
	crit_enter();
	inb(0x84);
	outb(IO_RTC, reg);
	inb(0x84);
	outb(IO_RTC + 1, val);
	inb(0x84);		/* XXX work around wrong order in rtcin() */
	crit_exit();
}

static __inline int
readrtc(int port)
{
	return(bcd2bin(rtcin(port)));
}

static u_int
calibrate_clocks(void)
{
	tsc_uclock_t old_tsc;
	sysclock_t tot_count;
	sysclock_t count, prev_count;
	int sec, start_sec, timeout;

	if (bootverbose)
	        kprintf("Calibrating clock(s) ...\n");
	if (!(rtcin(RTC_STATUSD) & RTCSD_PWR))
		goto fail;
	timeout = 100000000;

	/* Read the mc146818A seconds counter. */
	for (;;) {
		if (!(rtcin(RTC_STATUSA) & RTCSA_TUP)) {
			sec = rtcin(RTC_SEC);
			break;
		}
		if (--timeout == 0)
			goto fail;
	}

	/* Wait for the mC146818A seconds counter to change. */
	start_sec = sec;
	for (;;) {
		if (!(rtcin(RTC_STATUSA) & RTCSA_TUP)) {
			sec = rtcin(RTC_SEC);
			if (sec != start_sec)
				break;
		}
		if (--timeout == 0)
			goto fail;
	}

	/* Start keeping track of the i8254 counter. */
	prev_count = sys_cputimer->count();
	tot_count = 0;

	if (tsc_present)
		old_tsc = rdtsc();
	else
		old_tsc = 0;		/* shut up gcc */

	/*
	 * Wait for the mc146818A seconds counter to change.  Read the i8254
	 * counter for each iteration since this is convenient and only
	 * costs a few usec of inaccuracy. The timing of the final reads
	 * of the counters almost matches the timing of the initial reads,
	 * so the main cause of inaccuracy is the varying latency from
	 * inside getit() or rtcin(RTC_STATUSA) to the beginning of the
	 * rtcin(RTC_SEC) that returns a changed seconds count.  The
	 * maximum inaccuracy from this cause is < 10 usec on 486's.
	 */
	start_sec = sec;
	for (;;) {
		if (!(rtcin(RTC_STATUSA) & RTCSA_TUP))
			sec = rtcin(RTC_SEC);
		count = sys_cputimer->count();
		tot_count += (sysclock_t)(count - prev_count);
		prev_count = count;
		if (sec != start_sec)
			break;
		if (--timeout == 0)
			goto fail;
	}

	/*
	 * Read the cpu cycle counter.  The timing considerations are
	 * similar to those for the i8254 clock.
	 */
	if (tsc_present) {
		tsc_frequency = rdtsc() - old_tsc;
		if (bootverbose) {
			kprintf("TSC clock: %jd Hz (Method A)\n",
			    (intmax_t)tsc_frequency);
		}
	}
	tsc_oneus_approx = ((tsc_frequency|1) + 999999) / 1000000;

	kprintf("i8254 clock: %lu Hz\n", tot_count);
	return (tot_count);

fail:
	kprintf("failed, using default i8254 clock of %lu Hz\n",
		i8254_cputimer.freq);
	return (i8254_cputimer.freq);
}

static void
i8254_restore(void)
{
	timer0_state = ACQUIRED;

	clock_lock();

	/*
	 * Timer0 is our fine-grained variable clock interrupt
	 */
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_SWSTROBE | TIMER_16BIT);
	outb(TIMER_CNTR0, 2);	/* lsb */
	outb(TIMER_CNTR0, 0);	/* msb */
	clock_unlock();

	if (!i8254_nointr) {
		cputimer_intr_register(&i8254_cputimer_intr);
		cputimer_intr_select(&i8254_cputimer_intr, 0);
	}

	/*
	 * Timer1 or timer2 is our free-running clock, but only if another
	 * has not been selected.
	 */
	cputimer_register(&i8254_cputimer);
	cputimer_select(&i8254_cputimer, 0);
}

static void
i8254_cputimer_construct(struct cputimer *timer, sysclock_t oldclock)
{
 	int which;

	/*
	 * Should we use timer 1 or timer 2 ?
	 */
	which = 0;
	TUNABLE_INT_FETCH("hw.i8254.walltimer", &which);
	if (which != 1 && which != 2)
		which = 2;

	switch(which) {
	case 1:
		timer->name = "i8254_timer1";
		timer->type = CPUTIMER_8254_SEL1;
		i8254_walltimer_sel = TIMER_SEL1;
		i8254_walltimer_cntr = TIMER_CNTR1;
		timer1_state = ACQUIRED;
		break;
	case 2:
		timer->name = "i8254_timer2";
		timer->type = CPUTIMER_8254_SEL2;
		i8254_walltimer_sel = TIMER_SEL2;
		i8254_walltimer_cntr = TIMER_CNTR2;
		timer2_state = ACQUIRED;
		break;
	}

	timer->base = (oldclock + 0xFFFF) & 0xFFFFFFFFFFFF0000LU;

	clock_lock();
	outb(TIMER_MODE, i8254_walltimer_sel | TIMER_RATEGEN | TIMER_16BIT);
	outb(i8254_walltimer_cntr, 0);	/* lsb */
	outb(i8254_walltimer_cntr, 0);	/* msb */
	outb(IO_PPI, inb(IO_PPI) | 1);	/* bit 0: enable gate, bit 1: spkr */
	clock_unlock();
}

static void
i8254_cputimer_destruct(struct cputimer *timer)
{
	switch(timer->type) {
	case CPUTIMER_8254_SEL1:
	    timer1_state = RELEASED;
	    break;
	case CPUTIMER_8254_SEL2:
	    timer2_state = RELEASED;
	    break;
	default:
	    break;
	}
	timer->type = 0;
}

static void
rtc_restore(void)
{
	/* Restore all of the RTC's "status" (actually, control) registers. */
	writertc(RTC_STATUSB, RTCSB_24HR);
	writertc(RTC_STATUSA, rtc_statusa);
	writertc(RTC_STATUSB, rtc_statusb);
}

/*
 * Restore all the timers.
 *
 * This function is called to resynchronize our core timekeeping after a
 * long halt, e.g. from apm_default_resume() and friends.  It is also
 * called if after a BIOS call we have detected munging of the 8254.
 * It is necessary because cputimer_count() counter's delta may have grown
 * too large for nanouptime() and friends to handle, or (in the case of 8254
 * munging) might cause the SYSTIMER code to prematurely trigger.
 */
void
timer_restore(void)
{
	crit_enter();
	if (i8254_cputimer_disable == 0)
		i8254_restore();	/* restore timer_freq and hz */
	rtc_restore();			/* reenable RTC interrupts */
	crit_exit();
}

#define MAX_MEASURE_RETRIES	100

static u_int64_t
do_measure(u_int64_t timer_latency, u_int64_t *latency, sysclock_t *time,
    int *retries)
{
	u_int64_t tsc1, tsc2;
	u_int64_t threshold;
	sysclock_t val;
	int cnt = 0;

	do {
		if (cnt > MAX_MEASURE_RETRIES/2)
			threshold = timer_latency << 1;
		else
			threshold = timer_latency + (timer_latency >> 2);

		cnt++;
		tsc1 = rdtsc_ordered();
		val = sys_cputimer->count();
		tsc2 = rdtsc_ordered();
	} while (timer_latency > 0 && cnt < MAX_MEASURE_RETRIES &&
	    tsc2 - tsc1 > threshold);

	*retries = cnt - 1;
	*latency = tsc2 - tsc1;
	*time = val;
	return tsc1;
}

static u_int64_t
do_calibrate_cputimer(u_int usecs, u_int64_t timer_latency)
{
	if (calibrate_tsc_fast) {
		u_int64_t old_tsc1, start_lat1, new_tsc1, end_lat1;
		u_int64_t old_tsc2, start_lat2, new_tsc2, end_lat2;
		u_int64_t freq1, freq2;
		sysclock_t start1, end1, start2, end2;
		int retries1, retries2, retries3, retries4;

		DELAY(1000);
		old_tsc1 = do_measure(timer_latency, &start_lat1, &start1,
		    &retries1);
		DELAY(20000);
		old_tsc2 = do_measure(timer_latency, &start_lat2, &start2,
		    &retries2);
		DELAY(usecs);
		new_tsc1 = do_measure(timer_latency, &end_lat1, &end1,
		    &retries3);
		DELAY(20000);
		new_tsc2 = do_measure(timer_latency, &end_lat2, &end2,
		    &retries4);

		old_tsc1 += start_lat1;
		old_tsc2 += start_lat2;
		freq1 = (new_tsc1 - old_tsc1) + (start_lat1 + end_lat1) / 2;
		freq2 = (new_tsc2 - old_tsc2) + (start_lat2 + end_lat2) / 2;
		end1 -= start1;
		end2 -= start2;
		/* This should in practice be safe from overflows. */
		freq1 = muldivu64(freq1, sys_cputimer->freq, end1);
		freq2 = muldivu64(freq2, sys_cputimer->freq, end2);
		if (calibrate_test && (retries1 > 0 || retries2 > 0)) {
			kprintf("%s: retries: %d, %d, %d, %d\n",
			    __func__, retries1, retries2, retries3, retries4);
		}
		if (calibrate_test) {
			kprintf("%s: freq1=%ju freq2=%ju avg=%ju\n",
			    __func__, freq1, freq2, (freq1 + freq2) / 2);
		}
		return (freq1 + freq2) / 2;
	} else {
		u_int64_t old_tsc, new_tsc;
		u_int64_t freq;

		old_tsc = rdtsc_ordered();
		DELAY(usecs);
		new_tsc = rdtsc();
		freq = new_tsc - old_tsc;
		/* This should in practice be safe from overflows. */
		freq = (freq * 1000 * 1000) / usecs;
		return freq;
	}
}

/*
 * Initialize 8254 timer 0 early so that it can be used in DELAY().
 */
void
startrtclock(void)
{
	const timecounter_init_t **list;
	sysclock_t delta, freq;
	int forced_invariant = 0;

	callout_init_mp(&sysbeepstop_ch);

	/*
	 * Can we use the TSC?
	 *
	 * NOTE: If running under qemu, probably a good idea to force the
	 *	 TSC because we are not likely to detect it as being
	 *	 invariant or mpsyncd if you don't.  This will greatly
	 *	 reduce SMP contention.
	 */
	if (cpu_feature & CPUID_TSC) {
		tsc_present = 1;
		TUNABLE_INT_FETCH("hw.tsc_cputimer_force", &tsc_invariant);
		forced_invariant = tsc_invariant;

		if ((cpu_vendor_id == CPU_VENDOR_INTEL ||
		     cpu_vendor_id == CPU_VENDOR_AMD) &&
		    cpu_exthigh >= 0x80000007) {
			u_int regs[4];

			do_cpuid(0x80000007, regs);
			if (regs[3] & 0x100) {
				tsc_invariant = 1;
				forced_invariant = 0;
			}
		}
	} else {
		tsc_present = 0;
	}

	/*
	 * Initial RTC state, don't do anything unexpected
	 */
	writertc(RTC_STATUSA, rtc_statusa);
	writertc(RTC_STATUSB, RTCSB_24HR);

	SET_FOREACH(list, timecounter_init_set) {
		if ((*list)->configure != NULL)
			(*list)->configure();
	}

	/*
	 * If tsc_frequency is already initialized now, and a flag is set
	 * that i8254 timer is unneeded, we are done.
	 */
	if (tsc_frequency != 0 && i8254_cputimer_disable != 0)
		goto done;

	/*
	 * Set the 8254 timer0 in TIMER_SWSTROBE mode and cause it to
	 * generate an interrupt, which we will ignore for now.
	 *
	 * Set the 8254 timer1 in TIMER_RATEGEN mode and load 0x0000
	 * (so it counts a full 2^16 and repeats).  We will use this timer
	 * for our counting.
	 */
	if (i8254_cputimer_disable == 0)
		i8254_restore();

	kprintf("Using cputimer %s for TSC calibration\n", sys_cputimer->name);

	/*
	 * When booting without verbose messages, it's pointless to run the
	 * calibrate_clocks() calibration code, when we don't use the
	 * results in any way. With bootverbose, we are at least printing
	 *  this information to the kernel log.
	 */
	if (i8254_cputimer_disable != 0 ||
	    (calibrate_timers_with_rtc == 0 && !bootverbose)) {
		goto skip_rtc_based;
	}

	freq = calibrate_clocks();
#ifdef CLK_CALIBRATION_LOOP
	if (bootverbose) {
		int c;

		cnpoll(TRUE);
		kprintf("Press a key on the console to "
			"abort clock calibration\n");
		while ((c = cncheckc()) == -1 || c == NOKEY)
			calibrate_clocks();
		cnpoll(FALSE);
	}
#endif

	/*
	 * Use the calibrated i8254 frequency if it seems reasonable.
	 * Otherwise use the default, and don't use the calibrated i586
	 * frequency.
	 */
	delta = freq > i8254_cputimer.freq ?
		freq - i8254_cputimer.freq : i8254_cputimer.freq - freq;
	if (delta < i8254_cputimer.freq / 100) {
		if (calibrate_timers_with_rtc == 0) {
			kprintf(
"hw.calibrate_timers_with_rtc not set - using default i8254 frequency\n");
			freq = i8254_cputimer.freq;
		}
		/*
		 * NOTE:
		 * Interrupt timer's freq must be adjusted
		 * before we change the cuptimer's frequency.
		 */
		i8254_cputimer_intr.freq = freq;
		cputimer_set_frequency(&i8254_cputimer, freq);
	} else {
		if (bootverbose)
			kprintf("%lu Hz differs from default of %lu Hz "
				"by more than 1%%\n",
			        freq, i8254_cputimer.freq);
		tsc_frequency = 0;
	}

	if (tsc_frequency != 0 && calibrate_timers_with_rtc == 0) {
		kprintf("hw.calibrate_timers_with_rtc not "
			"set - using old calibration method\n");
		tsc_frequency = 0;
	}

skip_rtc_based:
	if (tsc_present && tsc_frequency == 0) {
		u_int cnt;
		u_int64_t cputime_latency_tsc = 0, max = 0, min = 0;
		int i;

		for (i = 0; i < 10; i++) {
			/* Warm up */
			(void)sys_cputimer->count();
		}
		for (i = 0; i < 100; i++) {
			u_int64_t old_tsc, new_tsc;

			old_tsc = rdtsc_ordered();
			(void)sys_cputimer->count();
			new_tsc = rdtsc_ordered();
			cputime_latency_tsc += (new_tsc - old_tsc);
			if (max < (new_tsc - old_tsc))
				max = new_tsc - old_tsc;
			if (min == 0 || min > (new_tsc - old_tsc))
				min = new_tsc - old_tsc;
		}
		cputime_latency_tsc /= 100;
		kprintf(
		    "Timer latency (in TSC ticks): %lu min=%lu max=%lu\n",
		    cputime_latency_tsc, min, max);
		/* XXX Instead of this, properly filter out outliers. */
		cputime_latency_tsc = min;

		if (calibrate_test > 0) {
			u_int64_t values[20], avg = 0;
			for (i = 1; i <= 20; i++) {
				u_int64_t freq;

				freq = do_calibrate_cputimer(i * 100 * 1000,
				    cputime_latency_tsc);
				values[i - 1] = freq;
			}
			/* Compute an average TSC for the 1s to 2s delays. */
			for (i = 10; i < 20; i++)
				avg += values[i];
			avg /= 10;
			for (i = 0; i < 20; i++) {
				kprintf("%ums: %lu (Diff from average: %ld)\n",
				    (i + 1) * 100, values[i],
				    (int64_t)(values[i] - avg));
			}
		}

		if (calibrate_tsc_fast > 0) {
			/* HPET would typically be >10MHz */
			if (sys_cputimer->freq >= 10000000)
				cnt = 200000;
			else
				cnt = 500000;
		} else {
			cnt = 1000000;
		}

		tsc_frequency = do_calibrate_cputimer(cnt, cputime_latency_tsc);
		if (bootverbose && calibrate_timers_with_rtc) {
			kprintf("TSC clock: %jd Hz (Method B)\n",
			    (intmax_t)tsc_frequency);
		}
	}

done:
	if (tsc_present) {
		kprintf("TSC clock: %jd Hz, %sinvariant%s",
			(intmax_t)tsc_frequency,
			tsc_invariant ? "" : "not ",
			forced_invariant ? " (forced)" : "");
	}
	tsc_oneus_approx = ((tsc_frequency|1) + 999999) / 1000000;

	EVENTHANDLER_REGISTER(shutdown_post_sync, resettodr_on_shutdown,
			      NULL, SHUTDOWN_PRI_LAST);
}

/*
 * Sync the time of day back to the RTC on shutdown, but only if
 * we have already loaded it and have not crashed.
 */
static void
resettodr_on_shutdown(void *arg __unused)
{
	if (rtc_loaded && panicstr == NULL) {
		resettodr();
	}
}

/*
 * Initialize the time of day register, based on the time base which is, e.g.
 * from a filesystem.
 */
void
inittodr(time_t base)
{
	time_t		sec, days;
	int		year, month;
	int		y, m;
	struct timespec ts;

	if (base) {
		ts.tv_sec = base;
		ts.tv_nsec = 0;
		set_timeofday(&ts);
	}

	/* Look if we have a RTC present and the time is valid */
	if (!(rtcin(RTC_STATUSD) & RTCSD_PWR))
		goto wrong_time;

	/* wait for time update to complete */
	/* If RTCSA_TUP is zero, we have at least 244us before next update */
	crit_enter();
	while (rtcin(RTC_STATUSA) & RTCSA_TUP) {
		crit_exit();
		crit_enter();
	}

	days = 0;
#ifdef USE_RTC_CENTURY
	year = readrtc(RTC_YEAR) + readrtc(RTC_CENTURY) * 100;
#else
	year = readrtc(RTC_YEAR) + 1900;
	if (year < 1970)
		year += 100;
#endif
	if (year < 1970) {
		crit_exit();
		goto wrong_time;
	}
	month = readrtc(RTC_MONTH);
	for (m = 1; m < month; m++)
		days += daysinmonth[m-1];
	if ((month > 2) && LEAPYEAR(year))
		days ++;
	days += readrtc(RTC_DAY) - 1;
	for (y = 1970; y < year; y++)
		days += DAYSPERYEAR + LEAPYEAR(y);
	sec = ((( days * 24 +
		  readrtc(RTC_HRS)) * 60 +
		  readrtc(RTC_MIN)) * 60 +
		  readrtc(RTC_SEC));
	/* sec now contains the number of seconds, since Jan 1 1970,
	   in the local time zone */

	sec += tz.tz_minuteswest * 60 + (wall_cmos_clock ? adjkerntz : 0);

	if (time_second <= sec - 2 || time_second >= sec + 2) {
		/* badly off, adjust it */
		ts.tv_sec = sec;
		ts.tv_nsec = 0;
		set_timeofday(&ts);
	}
	rtc_loaded = 1;
	crit_exit();
	return;

wrong_time:
	kprintf("Invalid time in real time clock.\n");
	kprintf("Check and reset the date immediately!\n");
}

/*
 * Write system time back to RTC
 */
void
resettodr(void)
{
	struct timeval tv;
	unsigned long tm;
	int m;
	int y;

	if (disable_rtc_set)
		return;

	microtime(&tv);
	tm = tv.tv_sec;

	crit_enter();
	/* Disable RTC updates and interrupts. */
	writertc(RTC_STATUSB, RTCSB_HALT | RTCSB_24HR);

	/* Calculate local time to put in RTC */

	tm -= tz.tz_minuteswest * 60 + (wall_cmos_clock ? adjkerntz : 0);

	writertc(RTC_SEC, bin2bcd(tm%60)); tm /= 60;	/* Write back Seconds */
	writertc(RTC_MIN, bin2bcd(tm%60)); tm /= 60;	/* Write back Minutes */
	writertc(RTC_HRS, bin2bcd(tm%24)); tm /= 24;	/* Write back Hours   */

	/* We have now the days since 01-01-1970 in tm */
	writertc(RTC_WDAY, (tm+4)%7);			/* Write back Weekday */
	for (y = 1970, m = DAYSPERYEAR + LEAPYEAR(y);
	     tm >= m;
	     y++,      m = DAYSPERYEAR + LEAPYEAR(y))
	     tm -= m;

	/* Now we have the years in y and the day-of-the-year in tm */
	writertc(RTC_YEAR, bin2bcd(y%100));		/* Write back Year    */
#ifdef USE_RTC_CENTURY
	writertc(RTC_CENTURY, bin2bcd(y/100));		/* ... and Century    */
#endif
	for (m = 0; ; m++) {
		int ml;

		ml = daysinmonth[m];
		if (m == 1 && LEAPYEAR(y))
			ml++;
		if (tm < ml)
			break;
		tm -= ml;
	}

	writertc(RTC_MONTH, bin2bcd(m + 1));            /* Write back Month   */
	writertc(RTC_DAY, bin2bcd(tm + 1));             /* Write back Month Day */

	/* Reenable RTC updates and interrupts. */
	writertc(RTC_STATUSB, rtc_statusb);
	crit_exit();
}

static int
i8254_ioapic_trial(int irq, struct cputimer_intr *cti)
{
	sysclock_t base;
	long lastcnt;

	/*
	 * Following code assumes the 8254 is the cpu timer,
	 * so make sure it is.
	 */
	/*KKASSERT(sys_cputimer == &i8254_cputimer); (tested by CuteLarva) */
	KKASSERT(cti == &i8254_cputimer_intr);

	lastcnt = get_interrupt_counter(irq, mycpuid);

	/*
	 * Force an 8254 Timer0 interrupt and wait 1/100s for
	 * it to happen, then see if we got it.
	 */
	kprintf("IOAPIC: testing 8254 interrupt delivery...");

	i8254_intr_reload(cti, sys_cputimer->fromus(2));
	base = sys_cputimer->count();
	while (sys_cputimer->count() - base < sys_cputimer->freq / 100)
		; /* nothing */

	if (get_interrupt_counter(irq, mycpuid) - lastcnt == 0) {
		kprintf(" failed\n");
		return ENOENT;
	} else {
		kprintf(" success\n");
	}
	return 0;
}

/*
 * Start both clocks running.  DragonFly note: the stat clock is no longer
 * used.  Instead, 8254 based systimers are used for all major clock
 * interrupts.
 */
static void
i8254_intr_initclock(struct cputimer_intr *cti, boolean_t selected)
{
	void *clkdesc = NULL;
	int irq = 0, mixed_mode = 0, error;

	KKASSERT(mycpuid == 0);

	if (!selected && i8254_intr_disable)
		goto nointr;

	/*
	 * The stat interrupt mask is different without the
	 * statistics clock.  Also, don't set the interrupt
	 * flag which would normally cause the RTC to generate
	 * interrupts.
	 */
	rtc_statusb = RTCSB_24HR;

	/* Finish initializing 8254 timer 0. */
	if (ioapic_enable) {
		irq = machintr_legacy_intr_find(0, INTR_TRIGGER_EDGE,
			INTR_POLARITY_HIGH);
		if (irq < 0) {
mixed_mode_setup:
			error = ioapic_conf_legacy_extint(0);
			if (!error) {
				irq = machintr_legacy_intr_find(0,
				    INTR_TRIGGER_EDGE, INTR_POLARITY_HIGH);
				if (irq < 0)
					error = ENOENT;
			}

			if (error) {
				if (!selected) {
					kprintf("IOAPIC: setup mixed mode for "
						"irq 0 failed: %d\n", error);
					goto nointr;
				} else {
					panic("IOAPIC: setup mixed mode for "
					      "irq 0 failed: %d\n", error);
				}
			}
			mixed_mode = 1;
		}
		clkdesc = register_int(irq, clkintr, NULL, "clk",
				       NULL,
				       INTR_EXCL | INTR_CLOCK |
				       INTR_NOPOLL | INTR_MPSAFE |
				       INTR_NOENTROPY, 0);
	} else {
		register_int(0, clkintr, NULL, "clk", NULL,
			     INTR_EXCL | INTR_CLOCK |
			     INTR_NOPOLL | INTR_MPSAFE |
			     INTR_NOENTROPY, 0);
	}

	/* Initialize RTC. */
	writertc(RTC_STATUSA, rtc_statusa);
	writertc(RTC_STATUSB, RTCSB_24HR);

	if (ioapic_enable) {
		error = i8254_ioapic_trial(irq, cti);
		if (error) {
			if (mixed_mode) {
				if (!selected) {
					kprintf("IOAPIC: mixed mode for irq %d "
						"trial failed: %d\n",
						irq, error);
					goto nointr;
				} else {
					panic("IOAPIC: mixed mode for irq %d "
					      "trial failed: %d\n", irq, error);
				}
			} else {
				kprintf("IOAPIC: warning 8254 is not connected "
					"to the correct pin, try mixed mode\n");
				unregister_int(clkdesc, 0);
				goto mixed_mode_setup;
			}
		}
	}
	return;

nointr:
	i8254_nointr = 1; /* don't try to register again */
	cputimer_intr_deregister(cti);
}

void
setstatclockrate(int newhz)
{
	if (newhz == RTC_PROFRATE)
		rtc_statusa = RTCSA_DIVIDER | RTCSA_PROF;
	else
		rtc_statusa = RTCSA_DIVIDER | RTCSA_NOPROF;
	writertc(RTC_STATUSA, rtc_statusa);
}

#if 0
static unsigned
tsc_get_timecount(struct timecounter *tc)
{
	return (rdtsc());
}
#endif

#ifdef KERN_TIMESTAMP
#define KERN_TIMESTAMP_SIZE 16384
static u_long tsc[KERN_TIMESTAMP_SIZE] ;
SYSCTL_OPAQUE(_debug, OID_AUTO, timestamp, CTLFLAG_RD, tsc,
	sizeof(tsc), "LU", "Kernel timestamps");
void
_TSTMP(u_int32_t x)
{
	static int i;

	tsc[i] = (u_int32_t)rdtsc();
	tsc[i+1] = x;
	i = i + 2;
	if (i >= KERN_TIMESTAMP_SIZE)
		i = 0;
	tsc[i] = 0; /* mark last entry */
}
#endif /* KERN_TIMESTAMP */

static int
hw_i8254_timestamp(SYSCTL_HANDLER_ARGS)
{
	sysclock_t count;
	uint64_t tscval;
	char buf[32];

	crit_enter();
	if (sys_cputimer == &i8254_cputimer)
		count = sys_cputimer->count();
	else
		count = 0;
	if (tsc_present)
		tscval = rdtsc();
	else
		tscval = 0;
	crit_exit();

	ksnprintf(buf, sizeof(buf), "%016lx %016lx", count, tscval);
	return(SYSCTL_OUT(req, buf, strlen(buf) + 1));
}

struct tsc_mpsync_info {
	volatile int		tsc_ready_cnt;
	volatile int		tsc_done_cnt;
	volatile int		tsc_command;
	volatile int		unused01[5];
	struct {
		uint64_t	v;
		uint64_t	unused02;
	} tsc_saved[MAXCPU];
} __cachealign;

#if 0
static void
tsc_mpsync_test_loop(struct tsc_mpsync_thr *info)
{
	struct globaldata *gd = mycpu;
	tsc_uclock_t test_end, test_begin;
	u_int i;

	if (bootverbose) {
		kprintf("cpu%d: TSC testing MP synchronization ...\n",
		    gd->gd_cpuid);
	}

	test_begin = rdtsc_ordered();
	/* Run test for 100ms */
	test_end = test_begin + (tsc_frequency / 10);

	arg->tsc_mpsync = 1;
	arg->tsc_target = test_begin;

#define TSC_TEST_TRYMAX		1000000	/* Make sure we could stop */
#define TSC_TEST_TRYMIN		50000

	for (i = 0; i < TSC_TEST_TRYMAX; ++i) {
		struct lwkt_cpusync cs;

		crit_enter();
		lwkt_cpusync_init(&cs, gd->gd_other_cpus,
		    tsc_mpsync_test_remote, arg);
		lwkt_cpusync_interlock(&cs);
		cpu_pause();
		arg->tsc_target = rdtsc_ordered();
		cpu_mfence();
		lwkt_cpusync_deinterlock(&cs);
		crit_exit();
		cpu_pause();

		if (!arg->tsc_mpsync) {
			kprintf("cpu%d: TSC is not MP synchronized @%u\n",
			    gd->gd_cpuid, i);
			break;
		}
		if (arg->tsc_target > test_end && i >= TSC_TEST_TRYMIN)
			break;
	}

#undef TSC_TEST_TRYMIN
#undef TSC_TEST_TRYMAX

	if (arg->tsc_target == test_begin) {
		kprintf("cpu%d: TSC does not tick?!\n", gd->gd_cpuid);
		/* XXX disable TSC? */
		tsc_invariant = 0;
		arg->tsc_mpsync = 0;
		return;
	}

	if (arg->tsc_mpsync && bootverbose) {
		kprintf("cpu%d: TSC is MP synchronized after %u tries\n",
		    gd->gd_cpuid, i);
	}
}

#endif

#define TSC_TEST_COUNT		50000

static void
tsc_mpsync_ap_thread(void *xinfo)
{
	struct tsc_mpsync_info *info = xinfo;
	int cpu = mycpuid;
	int i;

	/*
	 * Tell main loop that we are ready and wait for initiation
	 */
	atomic_add_int(&info->tsc_ready_cnt, 1);
	while (info->tsc_command == 0) {
		lwkt_force_switch();
	}

	/*
	 * Run test for 10000 loops or until tsc_done_cnt != 0 (another
	 * cpu has finished its test), then increment done.
	 */
	crit_enter();
	for (i = 0; i < TSC_TEST_COUNT && info->tsc_done_cnt == 0; ++i) {
		info->tsc_saved[cpu].v = rdtsc_ordered();
	}
	crit_exit();
	atomic_add_int(&info->tsc_done_cnt, 1);

	lwkt_exit();
}

static void
tsc_mpsync_test(void)
{
	enum { TSCOK, TSCNEG, TSCSPAN } error = TSCOK;
	int cpu;
	int try;

	if (!tsc_invariant) {
		/* Not even invariant TSC */
		kprintf("TSC is not invariant, "
			"no further tests will be performed\n");
		return;
	}

	if (ncpus == 1) {
		/* Only one CPU */
		tsc_mpsync = 1;
		return;
	}

	/*
	 * Forcing can be used w/qemu to reduce contention
	 */
	TUNABLE_INT_FETCH("hw.tsc_cputimer_force", &tsc_mpsync);

	if (tsc_mpsync == 0) {
		switch (cpu_vendor_id) {
		case CPU_VENDOR_INTEL:
			/*
			 * Intel probably works
			 */
			break;

		case CPU_VENDOR_AMD:
			/*
			 * For AMD 15h and 16h (i.e. The Bulldozer and Jaguar
			 * architectures) we have to watch out for
			 * Erratum 778:
			 *     "Processor Core Time Stamp Counters May
			 *      Experience Drift"
			 * This Erratum is only listed for cpus in Family
			 * 15h < Model 30h and for 16h < Model 30h.
			 *
			 * AMD < Bulldozer probably doesn't work
			 */
			if (CPUID_TO_FAMILY(cpu_id) == 0x15 ||
			    CPUID_TO_FAMILY(cpu_id) == 0x16) {
				if (CPUID_TO_MODEL(cpu_id) < 0x30)
					return;
			} else if (CPUID_TO_FAMILY(cpu_id) < 0x17) {
				return;
			}
			break;

		default:
			/* probably won't work */
			return;
		}
	} else if (tsc_mpsync < 0) {
		kprintf("TSC MP synchronization test is disabled\n");
		tsc_mpsync = 0;
		return;
	}

	/*
	 * Test even if forced to 1 above.  If forced, we will use the TSC
	 * even if the test fails.  (set forced to -1 to disable entirely).
	 */
	kprintf("TSC testing MP synchronization ...\n");
	kprintf("TSC testing MP: NOTE! CPU pwrsave will inflate latencies!\n");

	/*
	 * Test that the TSC is monotonically increasing across CPU
	 * switches.  Otherwise time will get really messed up if the
	 * TSC is selected as the timebase.
	 *
	 * Test 4 times
	 */
	for (try = 0; tsc_frequency && try < 4; ++try) {
		tsc_uclock_t last;
		tsc_uclock_t next;
		tsc_sclock_t delta;
		tsc_sclock_t lo_delta = 0x7FFFFFFFFFFFFFFFLL;
		tsc_sclock_t hi_delta = -0x7FFFFFFFFFFFFFFFLL;

		last = rdtsc();
		for (cpu = 0; cpu < ncpus; ++cpu) {
			lwkt_migratecpu(cpu);
			next = rdtsc();
			if (cpu == 0) {
				last = next;
				continue;
			}

			delta = next - last;
			if (delta < 0) {
				kprintf("TSC cpu-delta NEGATIVE: "
					"cpu %d to %d (%ld)\n",
					cpu - 1, cpu, delta);
				error = TSCNEG;
			}
			if (lo_delta > delta)
				lo_delta = delta;
			if (hi_delta < delta)
				hi_delta = delta;
			last = next;
		}
		last = rdtsc();
		for (cpu = ncpus - 2; cpu >= 0; --cpu) {
			lwkt_migratecpu(cpu);
			next = rdtsc();
			delta = next - last;
			if (delta <= 0) {
				kprintf("TSC cpu-delta WAS NEGATIVE! "
					"cpu %d to %d (%ld)\n",
					cpu + 1, cpu, delta);
				error = TSCNEG;
			}
			if (lo_delta > delta)
				lo_delta = delta;
			if (hi_delta < delta)
				hi_delta = delta;
			last = next;
		}
		kprintf("TSC cpu-delta test complete, %ldnS to %ldnS ",
			muldivu64(lo_delta, 1000000000, tsc_frequency),
			muldivu64(hi_delta, 1000000000, tsc_frequency));
		if (error != TSCOK) {
			kprintf("FAILURE\n");
			break;
		}
		kprintf("SUCCESS\n");
	}

	/*
	 * Test TSC MP synchronization on APs.
	 *
	 * Test 4 times.
	 */
	for (try = 0; tsc_frequency && try < 4; ++try) {
		struct tsc_mpsync_info info;
		uint64_t last;
		int64_t xworst;
		int64_t xdelta;
		int64_t delta;

		bzero(&info, sizeof(info));

		for (cpu = 0; cpu < ncpus; ++cpu) {
			thread_t td;
			lwkt_create(tsc_mpsync_ap_thread, &info, &td,
				    NULL, TDF_NOSTART, cpu,
				    "tsc mpsync %d", cpu);
			lwkt_setpri_initial(td, curthread->td_pri);
			lwkt_schedule(td);
		}
		while (info.tsc_ready_cnt != ncpus)
			lwkt_force_switch();

		/*
		 * All threads are ready, start the test and wait for
		 * completion.
		 */
		info.tsc_command = 1;
		while (info.tsc_done_cnt != ncpus)
			lwkt_force_switch();

		/*
		 * Process results
		 */
		last = info.tsc_saved[0].v;
		delta = 0;
		xworst = 0;
		for (cpu = 0; cpu < ncpus; ++cpu) {
			xdelta = (int64_t)(info.tsc_saved[cpu].v - last);
			last = info.tsc_saved[cpu].v;
			if (xdelta < 0)
				xdelta = -xdelta;
			if (xworst < xdelta)
				xworst = xdelta;
			delta += xdelta;

		}

		/*
		 * Result from attempt.  Break-out if we succeeds, otherwise
		 * try again (up to 4 times).  This might be in a VM so we
		 * need to be robust.
		 */
		kprintf("TSC cpu concurrency test complete, worst=%ldns, "
			"avg=%ldns ",
			muldivu64(xworst, 1000000000, tsc_frequency),
			muldivu64(delta / ncpus, 1000000000, tsc_frequency));
		if (delta / ncpus > tsc_frequency / 100) {
			kprintf("FAILURE\n");
		}
		if (delta / ncpus < tsc_frequency / 100000) {
			kprintf("SUCCESS\n");
			if (error == TSCOK)
				tsc_mpsync = 1;
			break;
		}
		kprintf("INDETERMINATE\n");
	}

	if (tsc_mpsync)
		kprintf("TSC is MP synchronized\n");
	else
		kprintf("TSC is not MP synchronized\n");
}
SYSINIT(tsc_mpsync, SI_BOOT2_FINISH_SMP, SI_ORDER_ANY, tsc_mpsync_test, NULL);

static SYSCTL_NODE(_hw, OID_AUTO, i8254, CTLFLAG_RW, 0, "I8254");
SYSCTL_UINT(_hw_i8254, OID_AUTO, freq, CTLFLAG_RD, &i8254_cputimer.freq, 0,
	    "frequency");
SYSCTL_PROC(_hw_i8254, OID_AUTO, timestamp, CTLTYPE_STRING|CTLFLAG_RD,
	    0, 0, hw_i8254_timestamp, "A", "");

SYSCTL_INT(_hw, OID_AUTO, tsc_present, CTLFLAG_RD,
	    &tsc_present, 0, "TSC Available");
SYSCTL_INT(_hw, OID_AUTO, tsc_invariant, CTLFLAG_RD,
	    &tsc_invariant, 0, "Invariant TSC");
SYSCTL_INT(_hw, OID_AUTO, tsc_mpsync, CTLFLAG_RD,
	    &tsc_mpsync, 0, "TSC is synchronized across CPUs");
SYSCTL_QUAD(_hw, OID_AUTO, tsc_frequency, CTLFLAG_RD,
	    &tsc_frequency, 0, "TSC Frequency");
