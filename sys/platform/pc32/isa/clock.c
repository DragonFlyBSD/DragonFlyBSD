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
 * $DragonFly: src/sys/platform/pc32/isa/clock.c,v 1.15 2004/07/20 04:12:08 dillon Exp $
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

#include "use_apm.h"
#include "use_mca.h"
#include "opt_clock.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#ifndef SMP
#include <sys/lock.h>
#endif
#include <sys/sysctl.h>
#include <sys/cons.h>
#include <sys/systimer.h>
#include <sys/globaldata.h>
#include <sys/thread2.h>
#include <sys/systimer.h>

#include <machine/clock.h>
#ifdef CLK_CALIBRATION_LOOP
#endif
#include <machine/cputypes.h>
#include <machine/frame.h>
#include <machine/ipl.h>
#include <machine/limits.h>
#include <machine/md_var.h>
#include <machine/psl.h>
#ifdef APIC_IO
#include <machine/segments.h>
#endif
#if defined(SMP) || defined(APIC_IO)
#include <machine/smp.h>
#endif /* SMP || APIC_IO */
#include <machine/specialreg.h>

#include <i386/isa/icu.h>
#include <bus/isa/i386/isa.h>
#include <bus/isa/rtc.h>
#include <i386/isa/timerreg.h>

#include <i386/isa/intr_machdep.h>

#if NMCA > 0
#include <bus/mca/i386/mca_machdep.h>
#endif

#ifdef APIC_IO
#include <i386/isa/intr_machdep.h>
/* The interrupt triggered by the 8254 (timer) chip */
int apic_8254_intr;
static u_long read_intr_count (int vec);
static void setup_8254_mixed_mode (void);
#endif
static void i8254_restore(void);

/*
 * 32-bit time_t's can't reach leap years before 1904 or after 2036, so we
 * can use a simple formula for leap years.
 */
#define	LEAPYEAR(y) ((u_int)(y) % 4 == 0)
#define DAYSPERYEAR   (31+28+31+30+31+30+31+31+30+31+30+31)

#ifndef TIMER_FREQ
#define TIMER_FREQ   1193182
#endif

#define TIMER_SELX	TIMER_SEL2
#define TIMER_CNTRX	TIMER_CNTR2

int	adjkerntz;		/* local offset from GMT in seconds */
int	disable_rtc_set;	/* disable resettodr() if != 0 */
volatile u_int	idelayed;
int	statclock_disable = 1;	/* we don't use the statclock right now */
u_int	stat_imask = SWI_CLOCK_MASK;
u_int	cputimer_freq = TIMER_FREQ;
#if 0
int64_t	cputimer_freq64_usec = ((int64_t)TIMER_FREQ << 32) / 1000000;
int64_t	cputimer_freq64_nsec = ((int64_t)TIMER_FREQ << 32) / 1000000000LL;
#endif
int64_t	cputimer_freq64_usec = (1000000LL << 32) / TIMER_FREQ;
int64_t	cputimer_freq64_nsec = (1000000000LL << 32) / TIMER_FREQ;
u_int	tsc_freq;
int	tsc_is_broken;
int	wall_cmos_clock;	/* wall CMOS clock assumed if != 0 */
int	timer0_running;
enum tstate { RELEASED, ACQUIRED };
enum tstate timer0_state;
enum tstate timer1_state;
enum tstate timer2_state;

static	int	beeping = 0;
static	u_int	clk_imask = HWI_MASK | SWI_MASK;
static	const u_char daysinmonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
static	u_char	rtc_statusa = RTCSA_DIVIDER | RTCSA_NOPROF;
static	u_char	rtc_statusb = RTCSB_24HR | RTCSB_PINTR;
static	u_int	tsc_present;

/*
 * timer0 clock interrupt.  Timer0 is in one-shot mode and has stopped
 * counting as of this interrupt.  We use timer1 in free-running mode (not
 * generating any interrupts) as our main counter.  Each cpu has timeouts
 * pending.
 */
static void
clkintr(struct intrframe frame)
{
	static sysclock_t timer1_count;
	struct globaldata *gd = mycpu;
	struct globaldata *gscan;
	int n;

	/*
	 * SWSTROBE mode is a one-shot, the timer is no longer running
	 */
	timer0_running = 0;

	/*
	 * XXX this could be done more efficiently by using a bitmask?
	 */
	timer1_count = cputimer_count();
	for (n = 0; n < ncpus; ++n) {
	    gscan = globaldata_find(n);
	    if (gscan->gd_nextclock == 0)
		continue;
	    if (gscan != gd) {
		lwkt_send_ipiq(gscan, (ipifunc_t)systimer_intr, &timer1_count);
	    } else {
		systimer_intr(&timer1_count, &frame);
	    }
	}
#if NMCA > 0
	/* Reset clock interrupt by asserting bit 7 of port 0x61 */
	if (MCA_system)
		outb(0x61, inb(0x61) | 0x80);
#endif
}


/*
 * NOTE! not MP safe.
 */
int
acquire_timer2(int mode)
{
	/* Timer2 is being used for time count operation */
	return(-1);
#if 0
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
#endif
}

int
release_timer2()
{
	if (timer2_state != ACQUIRED)
		return (-1);
	outb(TIMER_MODE, TIMER_SEL2 | TIMER_SQWAVE | TIMER_16BIT);
	timer2_state = RELEASED;
	return (0);
}

/*
 * This routine receives statistical clock interrupts from the RTC.
 * As explained above, these occur at 128 interrupts per second.
 * When profiling, we receive interrupts at a rate of 1024 Hz.
 *
 * This does not actually add as much overhead as it sounds, because
 * when the statistical clock is active, the hardclock driver no longer
 * needs to keep (inaccurate) statistics on its own.  This decouples
 * statistics gathering from scheduling interrupts.
 *
 * The RTC chip requires that we read status register C (RTC_INTR)
 * to acknowledge an interrupt, before it will generate the next one.
 * Under high interrupt load, rtcintr() can be indefinitely delayed and
 * the clock can tick immediately after the read from RTC_INTR.  In this
 * case, the mc146818A interrupt signal will not drop for long enough
 * to register with the 8259 PIC.  If an interrupt is missed, the stat
 * clock will halt, considerably degrading system performance.  This is
 * why we use 'while' rather than a more straightforward 'if' below.
 * Stat clock ticks can still be lost, causing minor loss of accuracy
 * in the statistics, but the stat clock will no longer stop.
 */
static void
rtcintr(struct intrframe frame)
{
	while (rtcin(RTC_INTR) & RTCIR_PERIOD)
		;
		/* statclock(&frame); no longer used */
}

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>

DB_SHOW_COMMAND(rtc, rtc)
{
	printf("%02x/%02x/%02x %02x:%02x:%02x, A = %02x, B = %02x, C = %02x\n",
	       rtcin(RTC_YEAR), rtcin(RTC_MONTH), rtcin(RTC_DAY),
	       rtcin(RTC_HRS), rtcin(RTC_MIN), rtcin(RTC_SEC),
	       rtcin(RTC_STATUSA), rtcin(RTC_STATUSB), rtcin(RTC_INTR));
}
#endif /* DDB */

/*
 * Convert a frequency to a cpu timer count.
 */
sysclock_t
cputimer_fromhz(int freq)
{
	return(cputimer_freq / freq + 1);
}

sysclock_t
cputimer_fromus(int us)
{
	return((int64_t)cputimer_freq * us / 1000000);
}

/*
 * Return the current cpu timer count as a 32 bit integer.
 */
sysclock_t
cputimer_count(void)
{
	static sysclock_t cputimer_base;
	static __uint16_t cputimer_last;
	__uint16_t count;
	sysclock_t ret;

	clock_lock();
	outb(TIMER_MODE, TIMER_SELX | TIMER_LATCH);
	count = (__uint8_t)inb(TIMER_CNTRX);		/* get countdown */
	count |= ((__uint8_t)inb(TIMER_CNTRX) << 8);
	count = -count;					/* -> countup */
	if (count < cputimer_last)			/* rollover */
		cputimer_base += 0x00010000;
	ret = cputimer_base | count;
	cputimer_last = count;
	clock_unlock();
	return(ret);
}

/*
 * Reload for the next timeout.  It is possible for the reload value
 * to be 0 or negative, indicating that an immediate timer interrupt
 * is desired.  For now make the minimum 2 ticks.
 */
void
cputimer_intr_reload(sysclock_t reload)
{
    __uint16_t count;

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
 * Wait "n" microseconds.
 * Relies on timer 1 counting down from (cputimer_freq / hz)
 * Note: timer had better have been programmed before this is first used!
 */
void
DELAY(int n)
{
	int delta, prev_tick, tick, ticks_left;

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
		printf("DELAY(%d)...", n);
#endif
	/*
	 * Guard against the timer being uninitialized if we are called
	 * early for console i/o.
	 */
	if (timer0_state == RELEASED)
		i8254_restore();

	/*
	 * Read the counter first, so that the rest of the setup overhead is
	 * counted.  Guess the initial overhead is 20 usec (on most systems it
	 * takes about 1.5 usec for each of the i/o's in getit().  The loop
	 * takes about 6 usec on a 486/33 and 13 usec on a 386/20.  The
	 * multiplications and divisions to scale the count take a while).
	 */
	prev_tick = cputimer_count();
	n -= 0;			/* XXX actually guess no initial overhead */
	/*
	 * Calculate (n * (cputimer_freq / 1e6)) without using floating point
	 * and without any avoidable overflows.
	 */
	if (n <= 0) {
		ticks_left = 0;
	} else if (n < 256) {
		/*
		 * Use fixed point to avoid a slow division by 1000000.
		 * 39099 = 1193182 * 2^15 / 10^6 rounded to nearest.
		 * 2^15 is the first power of 2 that gives exact results
		 * for n between 0 and 256.
		 */
		ticks_left = ((u_int)n * 39099 + (1 << 15) - 1) >> 15;
	} else {
		/*
		 * Don't bother using fixed point, although gcc-2.7.2
		 * generates particularly poor code for the long long
		 * division, since even the slow way will complete long
		 * before the delay is up (unless we're interrupted).
		 */
		ticks_left = ((u_int)n * (long long)cputimer_freq + 999999)
			     / 1000000;
	}

	while (ticks_left > 0) {
		tick = cputimer_count();
#ifdef DELAYDEBUG
		++getit_calls;
#endif
		delta = tick - prev_tick;
		prev_tick = tick;
		if (delta < 0)
			delta = 0;
		ticks_left -= delta;
	}
#ifdef DELAYDEBUG
	if (state == 1)
		printf(" %d calls to getit() at %d usec each\n",
		       getit_calls, (n + 5) / getit_calls);
#endif
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
	/*
	 * Nobody else is using timer2, we do not need the clock lock
	 */
	outb(TIMER_CNTR2, pitch);
	outb(TIMER_CNTR2, (pitch>>8));
	if (!beeping) {
		/* enable counter2 output to speaker */
		outb(IO_PPI, inb(IO_PPI) | 3);
		beeping = period;
		timeout(sysbeepstop, (void *)NULL, period);
	}
	return (0);
}

/*
 * RTC support routines
 */

int
rtcin(reg)
	int reg;
{
	int s;
	u_char val;

	s = splhigh();
	outb(IO_RTC, reg);
	inb(0x84);
	val = inb(IO_RTC + 1);
	inb(0x84);
	splx(s);
	return (val);
}

static __inline void
writertc(u_char reg, u_char val)
{
	int s;

	s = splhigh();
	inb(0x84);
	outb(IO_RTC, reg);
	inb(0x84);
	outb(IO_RTC + 1, val);
	inb(0x84);		/* XXX work around wrong order in rtcin() */
	splx(s);
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
	u_int count, prev_count, tot_count;
	int sec, start_sec, timeout;

	if (bootverbose)
	        printf("Calibrating clock(s) ... ");
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
	prev_count = cputimer_count();
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
		count = cputimer_count();
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
	if (tsc_present) 
		tsc_freq = rdtsc() - old_tsc;

	if (tsc_present)
		printf("TSC clock: %u Hz, ", tsc_freq);
	printf("i8254 clock: %u Hz\n", tot_count);
	return (tot_count);

fail:
	printf("failed, using default i8254 clock of %u Hz\n", cputimer_freq);
	return (cputimer_freq);
}

static void
i8254_restore(void)
{
	timer0_state = ACQUIRED;
	timer1_state = ACQUIRED;
	clock_lock();
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_SWSTROBE | TIMER_16BIT);
	outb(TIMER_CNTR0, 2);	/* lsb */
	outb(TIMER_CNTR0, 0);	/* msb */
	outb(TIMER_MODE, TIMER_SELX | TIMER_RATEGEN | TIMER_16BIT);
	outb(TIMER_CNTRX, 0);	/* lsb */
	outb(TIMER_CNTRX, 0);	/* msb */
	outb(IO_PPI, inb(IO_PPI) | 1);	/* bit 0: enable gate, bit 1: spkr */
	clock_unlock();
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
 * Restore all the timers non-atomically (XXX: should be atomically).
 *
 * This function is called from apm_default_resume() to restore all the timers.
 * This should not be necessary, but there are broken laptops that do not
 * restore all the timers on resume.
 */
void
timer_restore(void)
{
	i8254_restore();		/* restore timer_freq and hz */
	rtc_restore();			/* reenable RTC interrupts */
}

/*
 * Initialize 8254 timer 0 early so that it can be used in DELAY().
 */
void
startrtclock()
{
	u_int delta, freq;

	/* 
	 * Can we use the TSC?
	 */
	if (cpu_feature & CPUID_TSC)
		tsc_present = 1;
	else
		tsc_present = 0;

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
		printf(
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
	delta = freq > cputimer_freq ? 
			freq - cputimer_freq : cputimer_freq - freq;
	if (delta < cputimer_freq / 100) {
#ifndef CLK_USE_I8254_CALIBRATION
		if (bootverbose)
			printf(
"CLK_USE_I8254_CALIBRATION not specified - using default frequency\n");
		freq = cputimer_freq;
#endif
		cputimer_freq = freq;
		cputimer_freq64_usec = (1000000LL << 32) / freq;
		cputimer_freq64_nsec = (1000000000LL << 32) / freq;
	} else {
		if (bootverbose)
			printf(
		    "%d Hz differs from default of %d Hz by more than 1%%\n",
			       freq, cputimer_freq);
		tsc_freq = 0;
	}

#ifndef CLK_USE_TSC_CALIBRATION
	if (tsc_freq != 0) {
		if (bootverbose)
			printf(
"CLK_USE_TSC_CALIBRATION not specified - using old calibration method\n");
		tsc_freq = 0;
	}
#endif
	if (tsc_present && tsc_freq == 0) {
		/*
		 * Calibration of the i586 clock relative to the mc146818A
		 * clock failed.  Do a less accurate calibration relative
		 * to the i8254 clock.
		 */
		u_int64_t old_tsc = rdtsc();

		DELAY(1000000);
		tsc_freq = rdtsc() - old_tsc;
#ifdef CLK_USE_TSC_CALIBRATION
		if (bootverbose)
			printf("TSC clock: %u Hz (Method B)\n", tsc_freq);
#endif
	}

#if !defined(SMP)
	/*
	 * We can not use the TSC in SMP mode, until we figure out a
	 * cheap (impossible), reliable and precise (yeah right!)  way
	 * to synchronize the TSCs of all the CPUs.
	 * Curse Intel for leaving the counter out of the I/O APIC.
	 */

#if NAPM > 0
	/*
	 * We can not use the TSC if we support APM. Precise timekeeping
	 * on an APM'ed machine is at best a fools pursuit, since 
	 * any and all of the time spent in various SMM code can't 
	 * be reliably accounted for.  Reading the RTC is your only
	 * source of reliable time info.  The i8254 looses too of course
	 * but we need to have some kind of time...
	 * We don't know at this point whether APM is going to be used
	 * or not, nor when it might be activated.  Play it safe.
	 */
	return;
#endif /* NAPM > 0 */

#endif /* !defined(SMP) */
}

/*
 * Initialize the time of day register, based on the time base which is, e.g.
 * from a filesystem.
 */
void
inittodr(time_t base)
{
	unsigned long	sec, days;
	int		yd;
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
	yd = days;
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
	crit_exit();
	return;

wrong_time:
	printf("Invalid time in real time clock.\n");
	printf("Check and reset the date immediately!\n");
}

/*
 * Write system time back to RTC
 */
void
resettodr()
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


/*
 * Start both clocks running.  DragonFly note: the stat clock is no longer
 * used.  Instead, 8254 based systimers are used for all major clock
 * interrupts.  statclock_disable is set by default.
 */
void
cpu_initclocks()
{
	int diag;
#ifdef APIC_IO
	int apic_8254_trial;
	struct intrec *clkdesc;
#endif /* APIC_IO */

	if (statclock_disable) {
		/*
		 * The stat interrupt mask is different without the
		 * statistics clock.  Also, don't set the interrupt
		 * flag which would normally cause the RTC to generate
		 * interrupts.
		 */
		stat_imask = HWI_MASK | SWI_MASK;
		rtc_statusb = RTCSB_24HR;
	} else {
	        /* Setting stathz to nonzero early helps avoid races. */
		stathz = RTC_NOPROFRATE;
		profhz = RTC_PROFRATE;
        }

	/* Finish initializing 8253 timer 0. */
#ifdef APIC_IO

	apic_8254_intr = isa_apic_irq(0);
	apic_8254_trial = 0;
	if (apic_8254_intr >= 0 ) {
		if (apic_int_type(0, 0) == 3)
			apic_8254_trial = 1;
	} else {
		/* look for ExtInt on pin 0 */
		if (apic_int_type(0, 0) == 3) {
			apic_8254_intr = apic_irq(0, 0);
			setup_8254_mixed_mode();
		} else 
			panic("APIC_IO: Cannot route 8254 interrupt to CPU");
	}

	clkdesc = inthand_add("clk", apic_8254_intr, (inthand2_t *)clkintr,
			      NULL, &clk_imask, INTR_EXCL | INTR_FAST);
	INTREN(1 << apic_8254_intr);
	
#else /* APIC_IO */

	inthand_add("clk", 0, (inthand2_t *)clkintr, NULL, &clk_imask,
		    INTR_EXCL | INTR_FAST);
	INTREN(IRQ0);

#endif /* APIC_IO */

	/* Initialize RTC. */
	writertc(RTC_STATUSA, rtc_statusa);
	writertc(RTC_STATUSB, RTCSB_24HR);

	if (statclock_disable == 0) {
		diag = rtcin(RTC_DIAG);
		if (diag != 0)
			printf("RTC BIOS diagnostic error %b\n", diag, RTCDG_BITS);

#ifdef APIC_IO
		if (isa_apic_irq(8) != 8)
			panic("APIC RTC != 8");
#endif /* APIC_IO */

		inthand_add("rtc", 8, (inthand2_t *)rtcintr, NULL, &stat_imask,
			    INTR_EXCL | INTR_FAST);

#ifdef APIC_IO
		INTREN(APIC_IRQ8);
#else
		INTREN(IRQ8);
#endif /* APIC_IO */

		writertc(RTC_STATUSB, rtc_statusb);
	}

#ifdef APIC_IO
	if (apic_8254_trial) {
		sysclock_t base;
		int lastcnt = read_intr_count(apic_8254_intr);

		/*
		 * XXX this assumes the 8254 is the cpu timer.  Force an
		 * 8254 Timer0 interrupt and wait 1/100s for it to happen,
		 * then see if we got it.
		 */
		printf("APIC_IO: Testing 8254 interrupt delivery\n");
		cputimer_intr_reload(2);	/* XXX assumes 8254 */
		base = cputimer_count();
		while (cputimer_count() - base < cputimer_freq / 100)
			;	/* nothing */
		if (read_intr_count(apic_8254_intr) - lastcnt == 0) {
			/* 
			 * The MP table is broken.
			 * The 8254 was not connected to the specified pin
			 * on the IO APIC.
			 * Workaround: Limited variant of mixed mode.
			 */
			INTRDIS(1 << apic_8254_intr);
			inthand_remove(clkdesc);
			printf("APIC_IO: Broken MP table detected: "
			       "8254 is not connected to "
			       "IOAPIC #%d intpin %d\n",
			       int_to_apicintpin[apic_8254_intr].ioapic,
			       int_to_apicintpin[apic_8254_intr].int_pin);
			/* 
			 * Revoke current ISA IRQ 0 assignment and 
			 * configure a fallback interrupt routing from
			 * the 8254 Timer via the 8259 PIC to the
			 * an ExtInt interrupt line on IOAPIC #0 intpin 0.
			 * We reuse the low level interrupt handler number.
			 */
			if (apic_irq(0, 0) < 0) {
				revoke_apic_irq(apic_8254_intr);
				assign_apic_irq(0, 0, apic_8254_intr);
			}
			apic_8254_intr = apic_irq(0, 0);
			setup_8254_mixed_mode();
			inthand_add("clk", apic_8254_intr,
				    (inthand2_t *)clkintr,
				    NULL, &clk_imask, INTR_EXCL | INTR_FAST);
			INTREN(1 << apic_8254_intr);
		}
		
	}
	if (apic_int_type(0, 0) != 3 ||
	    int_to_apicintpin[apic_8254_intr].ioapic != 0 ||
	    int_to_apicintpin[apic_8254_intr].int_pin != 0) {
		printf("APIC_IO: routing 8254 via IOAPIC #%d intpin %d\n",
		       int_to_apicintpin[apic_8254_intr].ioapic,
		       int_to_apicintpin[apic_8254_intr].int_pin);
	} else {
		printf("APIC_IO: "
		       "routing 8254 via 8259 and IOAPIC #0 intpin 0\n");
	}
#endif
	
}

#ifdef APIC_IO
static u_long
read_intr_count(int vec)
{
	u_long *up;
	up = intr_countp[vec];
	if (up)
		return *up;
	return 0UL;
}

static void 
setup_8254_mixed_mode()
{
	/*
	 * Allow 8254 timer to INTerrupt 8259:
	 *  re-initialize master 8259:
	 *   reset; prog 4 bytes, single ICU, edge triggered
	 */
	outb(IO_ICU1, 0x13);
	outb(IO_ICU1 + 1, NRSVIDT);	/* start vector (unused) */
	outb(IO_ICU1 + 1, 0x00);	/* ignore slave */
	outb(IO_ICU1 + 1, 0x03);	/* auto EOI, 8086 */
	outb(IO_ICU1 + 1, 0xfe);	/* unmask INT0 */
	
	/* program IO APIC for type 3 INT on INT0 */
	if (ext_int_setup(0, 0) < 0)
		panic("8254 redirect via APIC pin0 impossible!");
}
#endif

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
    count = cputimer_count();
    if (tsc_present)
	tscval = rdtsc();
    else
	tscval = 0;
    crit_exit();
    snprintf(buf, sizeof(buf), "%08x %016llx", count, (long long)tscval);
    return(SYSCTL_OUT(req, buf, strlen(buf) + 1));
}

SYSCTL_NODE(_hw, OID_AUTO, i8254, CTLFLAG_RW, 0, "I8254");
SYSCTL_UINT(_hw_i8254, OID_AUTO, freq, CTLFLAG_RD, &cputimer_freq, 0, "");
SYSCTL_PROC(_hw_i8254, OID_AUTO, timestamp, CTLTYPE_STRING|CTLFLAG_RD,
		0, 0, hw_i8254_timestamp, "A", "");

