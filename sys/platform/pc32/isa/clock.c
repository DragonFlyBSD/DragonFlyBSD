/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#include "opt_clock.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/sysctl.h>
#include <sys/cons.h>
#include <sys/systimer.h>
#include <sys/globaldata.h>
#include <sys/thread2.h>
#include <sys/systimer.h>
#include <sys/machintr.h>
#include <sys/interrupt.h>

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

#include <machine_base/apic/ioapic.h>
#include <machine_base/apic/ioapic_abi.h>
#include <machine_base/icu/icu.h>
#include <bus/isa/isa.h>
#include <bus/isa/rtc.h>
#include <machine_base/isa/timerreg.h>

#include <machine/intr_machdep.h>

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

int	adjkerntz;		/* local offset from GMT in seconds */
int	disable_rtc_set;	/* disable resettodr() if != 0 */
int	tsc_present;
int	tsc_invariant;
int64_t	tsc_frequency;
int	tsc_is_broken;
int	wall_cmos_clock;	/* wall CMOS clock assumed if != 0 */
int	timer0_running;
enum tstate { RELEASED, ACQUIRED };
enum tstate timer0_state;
enum tstate timer1_state;
enum tstate timer2_state;

static	int	beeping = 0;
static	const u_char daysinmonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
static	u_char	rtc_statusa = RTCSA_DIVIDER | RTCSA_NOPROF;
static	u_char	rtc_statusb = RTCSB_24HR | RTCSB_PINTR;
static  int	rtc_loaded;

static int i8254_cputimer_div;

static int i8254_nointr;
static int i8254_intr_disable = 1;
TUNABLE_INT("hw.i8254.intr_disable", &i8254_intr_disable);

static struct callout sysbeepstop_ch;

static sysclock_t i8254_cputimer_count(void);
static void i8254_cputimer_construct(struct cputimer *cputimer, sysclock_t last);
static void i8254_cputimer_destruct(struct cputimer *cputimer);

static struct cputimer	i8254_cputimer = {
    SLIST_ENTRY_INITIALIZER,
    "i8254",
    CPUTIMER_PRI_8254,
    0,
    i8254_cputimer_count,
    cputimer_default_fromhz,
    cputimer_default_fromus,
    i8254_cputimer_construct,
    i8254_cputimer_destruct,
    TIMER_FREQ,
    0, 0, 0
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
    .next = SLIST_ENTRY_INITIALIZER,
    .name = "i8254",
    .type = CPUTIMER_INTR_8254,
    .prio = CPUTIMER_INTR_PRIO_8254,
    .caps = CPUTIMER_INTR_CAP_PS
};

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
 * Return the current cpu timer count as a 32 bit integer.
 */
static
sysclock_t
i8254_cputimer_count(void)
{
	static __uint16_t cputimer_last;
	__uint16_t count;
	sysclock_t ret;

	clock_lock();
	outb(TIMER_MODE, i8254_walltimer_sel | TIMER_LATCH);
	count = (__uint8_t)inb(i8254_walltimer_cntr);		/* get countdown */
	count |= ((__uint8_t)inb(i8254_walltimer_cntr) << 8);
	count = -count;					/* -> countup */
	if (count < cputimer_last)			/* rollover */
		i8254_cputimer.base += 0x00010000;
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
 * bit arithmatic is required every time the interrupt timer is reloaded.
 */
static void
i8254_intr_config(struct cputimer_intr *cti, const struct cputimer *timer)
{
    int freq;
    int div;

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
    __uint16_t count;

    if (i8254_cputimer_div)
	reload /= i8254_cputimer_div;
    else
	reload = (int64_t)reload * cti->freq / sys_cputimer->freq;

    if ((int)reload < 2)
	reload = 2;

    clock_lock();
    if (timer0_running) {
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);	/* count-down timer */
	count = (__uint8_t)inb(TIMER_CNTR0);		/* lsb */
	count |= ((__uint8_t)inb(TIMER_CNTR0) << 8);	/* msb */
	if (reload < count) {
	    outb(TIMER_MODE, TIMER_SEL0 | TIMER_SWSTROBE | TIMER_16BIT);
	    outb(TIMER_CNTR0, (__uint8_t)reload); 	/* lsb */
	    outb(TIMER_CNTR0, (__uint8_t)(reload >> 8)); /* msb */
	}
    } else {
	timer0_running = 1;
	if (reload > 0xFFFF)
	    reload = 0;		/* full count */
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_SWSTROBE | TIMER_16BIT);
	outb(TIMER_CNTR0, (__uint8_t)reload); 		/* lsb */
	outb(TIMER_CNTR0, (__uint8_t)(reload >> 8));	/* msb */
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
	if (timer0_state == RELEASED)
		i8254_restore();

	/*
	 * Read the counter first, so that the rest of the setup overhead is
	 * counted.  Then calculate the number of hardware timer ticks
	 * required, rounding up to be sure we delay at least the requested
	 * number of microseconds.
	 */
	prev_tick = sys_cputimer->count();
	ticks_left = ((u_int)n * (int64_t)sys_cputimer->freq + 999999) /
		     1000000;

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
 * DELAY() never switches
 */
void
DELAY(int n)
{
	DODELAY(n, 0);
}

int
CHECKTIMEOUT(TOTALDELAY *tdd)
{
	sysclock_t delta;
	int us;

	if (tdd->started == 0) {
	       if (timer0_state == RELEASED)
		       i8254_restore();
	       tdd->last_clock = sys_cputimer->count();
	       tdd->started = 1;
	       return(0);
	}
	delta = sys_cputimer->count() - tdd->last_clock;
	us = (u_int64_t)delta * (u_int64_t)1000000 /
	     (u_int64_t)sys_cputimer->freq;
	tdd->last_clock += (u_int64_t)us * (u_int64_t)sys_cputimer->freq /
			   1000000;
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
	u_int64_t old_tsc;
	u_int tot_count;
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
		tot_count += (int)(count - prev_count);
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
	}

	if (tsc_present) {
		kprintf("TSC%s clock: %llu Hz, ",
		    tsc_invariant ? " invariant" : "",
		    (long long)tsc_frequency);
	}
	kprintf("i8254 clock: %u Hz\n", tot_count);
	return (tot_count);

fail:
	kprintf("failed, using default i8254 clock of %u Hz\n",
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

	timer->base = (oldclock + 0xFFFF) & ~0xFFFF;

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
	i8254_restore();		/* restore timer_freq and hz */
	rtc_restore();			/* reenable RTC interrupts */
	crit_exit();
}

/*
 * Initialize 8254 timer 0 early so that it can be used in DELAY().
 */
void
startrtclock(void)
{
	u_int delta, freq;

	/* 
	 * Can we use the TSC?
	 */
	if (cpu_feature & CPUID_TSC) {
		tsc_present = 1;
		if ((cpu_vendor_id == CPU_VENDOR_INTEL ||
		     cpu_vendor_id == CPU_VENDOR_AMD) &&
		    cpu_exthigh >= 0x80000007) {
			u_int regs[4];

			do_cpuid(0x80000007, regs);
			if (regs[3] & 0x100)
				tsc_invariant = 1;
		}
	} else {
		tsc_present = 0;
	}

	/*
	 * Initial RTC state, don't do anything unexpected
	 */
	writertc(RTC_STATUSA, rtc_statusa);
	writertc(RTC_STATUSB, RTCSB_24HR);

	/*
	 * Set the 8254 timer0 in TIMER_SWSTROBE mode and cause it to 
	 * generate an interrupt, which we will ignore for now.
	 *
	 * Set the 8254 timer1 in TIMER_RATEGEN mode and load 0x0000
	 * (so it counts a full 2^16 and repeats).  We will use this timer
	 * for our counting.
	 */
	i8254_restore();
	freq = calibrate_clocks();
#ifdef CLK_CALIBRATION_LOOP
	if (bootverbose) {
		kprintf(
		"Press a key on the console to abort clock calibration\n");
		while (cncheckc() == -1)
			calibrate_clocks();
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
#ifndef CLK_USE_I8254_CALIBRATION
		if (bootverbose)
			kprintf(
"CLK_USE_I8254_CALIBRATION not specified - using default frequency\n");
		freq = i8254_cputimer.freq;
#endif
		/*
		 * NOTE:
		 * Interrupt timer's freq must be adjusted
		 * before we change the cuptimer's frequency.
		 */
		i8254_cputimer_intr.freq = freq;
		cputimer_set_frequency(&i8254_cputimer, freq);
	} else {
		if (bootverbose)
			kprintf(
		    "%d Hz differs from default of %d Hz by more than 1%%\n",
			       freq, i8254_cputimer.freq);
		tsc_frequency = 0;
	}

#ifndef CLK_USE_TSC_CALIBRATION
	if (tsc_frequency != 0) {
		if (bootverbose)
			kprintf(
"CLK_USE_TSC_CALIBRATION not specified - using old calibration method\n");
		tsc_frequency = 0;
	}
#endif
	if (tsc_present && tsc_frequency == 0) {
		/*
		 * Calibration of the i586 clock relative to the mc146818A
		 * clock failed.  Do a less accurate calibration relative
		 * to the i8254 clock.
		 */
		u_int64_t old_tsc = rdtsc();

		DELAY(1000000);
		tsc_frequency = rdtsc() - old_tsc;
#ifdef CLK_USE_TSC_CALIBRATION
		if (bootverbose) {
			kprintf("TSC clock: %llu Hz (Method B)\n",
				tsc_frequency);
		}
#endif
	}

	EVENTHANDLER_REGISTER(shutdown_post_sync, resettodr_on_shutdown, NULL, SHUTDOWN_PRI_LAST);
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
	unsigned long	sec, days;
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

	y = time_second - sec;
	if (y <= -2 || y >= 2) {
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
	KKASSERT(sys_cputimer == &i8254_cputimer);
	KKASSERT(cti == &i8254_cputimer_intr);

	lastcnt = get_interrupt_counter(irq, mycpuid);

	/*
	 * Force an 8254 Timer0 interrupt and wait 1/100s for
	 * it to happen, then see if we got it.
	 */
	kprintf("IOAPIC: testing 8254 interrupt delivery\n");

	i8254_intr_reload(cti, 2);
	base = sys_cputimer->count();
	while (sys_cputimer->count() - base < sys_cputimer->freq / 100)
		; /* nothing */

	if (get_interrupt_counter(irq, mycpuid) - lastcnt == 0)
		return ENOENT;
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
	callout_init(&sysbeepstop_ch);

	if (!selected && i8254_intr_disable)
		goto nointr;

	/*
	 * The stat interrupt mask is different without the
	 * statistics clock.  Also, don't set the interrupt
	 * flag which would normally cause the RTC to generate
	 * interrupts.
	 */
	rtc_statusb = RTCSB_24HR;

	/* Finish initializing 8253 timer 0. */
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

/*
 *
 */

static int
hw_i8254_timestamp(SYSCTL_HANDLER_ARGS)
{
    sysclock_t count;
    __uint64_t tscval;
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
    ksnprintf(buf, sizeof(buf), "%08x %016llx", count, (long long)tscval);
    return(SYSCTL_OUT(req, buf, strlen(buf) + 1));
}

SYSCTL_NODE(_hw, OID_AUTO, i8254, CTLFLAG_RW, 0, "I8254");
SYSCTL_UINT(_hw_i8254, OID_AUTO, freq, CTLFLAG_RD, &i8254_cputimer.freq, 0,
	    "frequency");
SYSCTL_PROC(_hw_i8254, OID_AUTO, timestamp, CTLTYPE_STRING|CTLFLAG_RD,
	    0, 0, hw_i8254_timestamp, "A", "");

SYSCTL_INT(_hw, OID_AUTO, tsc_present, CTLFLAG_RD,
	    &tsc_present, 0, "TSC Available");
SYSCTL_INT(_hw, OID_AUTO, tsc_invariant, CTLFLAG_RD,
	    &tsc_invariant, 0, "Invariant TSC");
SYSCTL_QUAD(_hw, OID_AUTO, tsc_frequency, CTLFLAG_RD,
	    &tsc_frequency, 0, "TSC Frequency");
