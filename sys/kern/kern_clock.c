/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * Copyright (c) 1997, 1998 Poul-Henning Kamp <phk@FreeBSD.org>
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)kern_clock.c	8.5 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/kern_clock.c,v 1.105.2.10 2002/10/17 13:19:40 maxim Exp $
 * $DragonFly: src/sys/kern/kern_clock.c,v 1.23 2004/08/02 23:20:30 dillon Exp $
 */

#include "opt_ntp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/dkstat.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/timex.h>
#include <sys/timepps.h>
#include <vm/vm.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <sys/sysctl.h>
#include <sys/thread2.h>

#include <machine/cpu.h>
#include <machine/limits.h>
#include <machine/smp.h>

#ifdef GPROF
#include <sys/gmon.h>
#endif

#ifdef DEVICE_POLLING
extern void init_device_poll(void);
extern void hardclock_device_poll(void);
#endif /* DEVICE_POLLING */

static void initclocks (void *dummy);
SYSINIT(clocks, SI_SUB_CLOCKS, SI_ORDER_FIRST, initclocks, NULL)

/*
 * Some of these don't belong here, but it's easiest to concentrate them.
 * Note that cp_time[] counts in microseconds, but most userland programs
 * just compare relative times against the total by delta.
 */
long cp_time[CPUSTATES];

SYSCTL_OPAQUE(_kern, OID_AUTO, cp_time, CTLFLAG_RD, &cp_time, sizeof(cp_time),
    "LU", "CPU time statistics");

long tk_cancc;
long tk_nin;
long tk_nout;
long tk_rawcc;

/*
 * boottime is used to calculate the 'real' uptime.  Do not confuse this with
 * microuptime().  microtime() is not drift compensated.  The real uptime
 * with compensation is nanotime() - bootime.  boottime is recalculated
 * whenever the real time is set based on the compensated elapsed time
 * in seconds (gd->gd_time_seconds).
 *
 * basetime is used to calculate the compensated real time of day.  Chunky
 * changes to the time, aka settimeofday(), are made by modifying basetime.
 *
 * The gd_time_seconds and gd_cpuclock_base fields remain fairly monotonic.
 * Slight adjustments to gd_cpuclock_base are made to phase-lock it to
 * the real time.
 */
struct timespec boottime;	/* boot time (realtime) for reference only */
struct timespec basetime;	/* base time adjusts uptime -> realtime */
time_t time_second;		/* read-only 'passive' uptime in seconds */

SYSCTL_STRUCT(_kern, KERN_BOOTTIME, boottime, CTLFLAG_RD,
    &boottime, timeval, "System boottime");
SYSCTL_STRUCT(_kern, OID_AUTO, basetime, CTLFLAG_RD,
    &basetime, timeval, "System basetime");

static void hardclock(systimer_t info, struct intrframe *frame);
static void statclock(systimer_t info, struct intrframe *frame);
static void schedclock(systimer_t info, struct intrframe *frame);

int	ticks;			/* system master ticks at hz */
int	clocks_running;		/* tsleep/timeout clocks operational */
int64_t	nsec_adj;		/* ntpd per-tick adjustment in nsec << 32 */
int64_t	nsec_acc;		/* accumulator */

/*
 * Finish initializing clock frequencies and start all clocks running.
 */
/* ARGSUSED*/
static void
initclocks(void *dummy)
{
	cpu_initclocks();
#ifdef DEVICE_POLLING
	init_device_poll();
#endif
	/*psratio = profhz / stathz;*/
	initclocks_pcpu();
	clocks_running = 1;
}

/*
 * Called on a per-cpu basis
 */
void
initclocks_pcpu(void)
{
	struct globaldata *gd = mycpu;

	crit_enter();
	if (gd->gd_cpuid == 0) {
	    gd->gd_time_seconds = 1;
	    gd->gd_cpuclock_base = cputimer_count();
	} else {
	    /* XXX */
	    gd->gd_time_seconds = globaldata_find(0)->gd_time_seconds;
	    gd->gd_cpuclock_base = globaldata_find(0)->gd_cpuclock_base;
	}
	systimer_init_periodic(&gd->gd_hardclock, hardclock, NULL, hz);
	systimer_init_periodic(&gd->gd_statclock, statclock, NULL, stathz);
	/* XXX correct the frequency for scheduler / estcpu tests */
	systimer_init_periodic(&gd->gd_schedclock, schedclock, 
				NULL, ESTCPUFREQ); 
	crit_exit();
}

/*
 * Resynchronize gd_cpuclock_base after the system has been woken up from 
 * a sleep.  It is absolutely essential that all the cpus be properly
 * synchronized.  Resynching is required because nanouptime() and friends
 * will overflow intermediate multiplications if more then 2 seconds
 * worth of cputimer_cont() delta has built up.
 */
#ifdef SMP

static
void
restoreclocks_remote(lwkt_cpusync_t poll)
{
	mycpu->gd_cpuclock_base = *(sysclock_t *)poll->cs_data;
	mycpu->gd_time_seconds = globaldata_find(0)->gd_time_seconds;
}

#endif

void
restoreclocks(void)
{
	sysclock_t base = cputimer_count();
#ifdef SMP
	lwkt_cpusync_simple(-1, restoreclocks_remote, &base);
#else
	mycpu->gd_cpuclock_base = base;
#endif
}

/*
 * This sets the current real time of day.  Timespecs are in seconds and
 * nanoseconds.  We do not mess with gd_time_seconds and gd_cpuclock_base,
 * instead we adjust basetime so basetime + gd_* results in the current
 * time of day.  This way the gd_* fields are guarenteed to represent
 * a monotonically increasing 'uptime' value.
 */
void
set_timeofday(struct timespec *ts)
{
	struct timespec ts2;

	/*
	 * XXX SMP / non-atomic basetime updates
	 */
	crit_enter();
	nanouptime(&ts2);
	basetime.tv_sec = ts->tv_sec - ts2.tv_sec;
	basetime.tv_nsec = ts->tv_nsec - ts2.tv_nsec;
	if (basetime.tv_nsec < 0) {
	    basetime.tv_nsec += 1000000000;
	    --basetime.tv_sec;
	}
	boottime.tv_sec = basetime.tv_sec - mycpu->gd_time_seconds;
	timedelta = 0;
	crit_exit();
}
	
/*
 * Each cpu has its own hardclock, but we only increments ticks and softticks
 * on cpu #0.
 *
 * NOTE! systimer! the MP lock might not be held here.  We can only safely
 * manipulate objects owned by the current cpu.
 */
static void
hardclock(systimer_t info, struct intrframe *frame)
{
	sysclock_t cputicks;
	struct proc *p;
	struct pstats *pstats;
	struct globaldata *gd = mycpu;

	/*
	 * Realtime updates are per-cpu.  Note that timer corrections as
	 * returned by microtime() and friends make an additional adjustment
	 * using a system-wise 'basetime', but the running time is always
	 * taken from the per-cpu globaldata area.  Since the same clock
	 * is distributing (XXX SMP) to all cpus, the per-cpu timebases
	 * stay in synch.
	 *
	 * Note that we never allow info->time (aka gd->gd_hardclock.time)
	 * to reverse index gd_cpuclock_base.
	 */
	cputicks = info->time - gd->gd_cpuclock_base;
	if (cputicks > cputimer_freq) {
		++gd->gd_time_seconds;
		gd->gd_cpuclock_base += cputimer_freq;
	}

	/*
	 * The system-wide ticks and softticks are only updated by cpu #0.
	 * Callwheel actions are also (at the moment) only handled by cpu #0.
	 * Finally, we also do NTP related timedelta/tickdelta adjustments
	 * by adjusting basetime.
	 */
	if (gd->gd_cpuid == 0) {
	    struct timespec nts;
	    int leap;

	    ++ticks;

#ifdef DEVICE_POLLING
	    hardclock_device_poll();	/* mpsafe, short and quick */
#endif /* DEVICE_POLLING */

	    if (TAILQ_FIRST(&callwheel[ticks & callwheelmask]) != NULL) {
		setsoftclock();
	    } else if (softticks + 1 == ticks) {
		++softticks;
	    }

#if 0
	    if (tco->tc_poll_pps) 
		tco->tc_poll_pps(tco);
#endif
	    /*
	     * Apply adjtime corrections.  At the moment only do this if 
	     * we can get the MP lock to interlock with adjtime's modification
	     * of these variables.  Note that basetime adjustments are not
	     * MP safe either XXX.
	     */
	    if (timedelta != 0 && try_mplock()) {
		basetime.tv_nsec += tickdelta * 1000;
		if (basetime.tv_nsec >= 1000000000) {
		    basetime.tv_nsec -= 1000000000;
		    ++basetime.tv_sec;
		} else if (basetime.tv_nsec < 0) {
		    basetime.tv_nsec += 1000000000;
		    --basetime.tv_sec;
		}
		timedelta -= tickdelta;
		rel_mplock();
	    }

	    /*
	     * Apply per-tick compensation.  ticks_adj adjusts for both
	     * offset and frequency, and could be negative.
	     */
	    if (nsec_adj != 0 && try_mplock()) {
		nsec_acc += nsec_adj;
		if (nsec_acc >= 0x100000000LL) {
		    basetime.tv_nsec += nsec_acc >> 32;
		    nsec_acc = (nsec_acc & 0xFFFFFFFFLL);
		} else if (nsec_acc <= -0x100000000LL) {
		    basetime.tv_nsec -= -nsec_acc >> 32;
		    nsec_acc = -(-nsec_acc & 0xFFFFFFFFLL);
		}
		if (basetime.tv_nsec >= 1000000000) {
		    basetime.tv_nsec -= 1000000000;
		    ++basetime.tv_sec;
		} else if (basetime.tv_nsec < 0) {
		    basetime.tv_nsec += 1000000000;
		    --basetime.tv_sec;
		}
		rel_mplock();
	    }

	    /*
	     * If the realtime-adjusted seconds hand rolls over then tell
	     * ntp_update_second() what we did in the last second so it can
	     * calculate what to do in the next second.  It may also add
	     * or subtract a leap second.
	     */
	    getnanotime(&nts);
	    if (time_second != nts.tv_sec) {
		leap = ntp_update_second(time_second, &nsec_adj);
		basetime.tv_sec += leap;
		time_second = nts.tv_sec + leap;
		nsec_adj /= hz;
	    }
	}

	/*
	 * ITimer handling is per-tick, per-cpu.  I don't think psignal()
	 * is mpsafe on curproc, so XXX get the mplock.
	 */
	if ((p = curproc) != NULL && try_mplock()) {
		pstats = p->p_stats;
		if (frame && CLKF_USERMODE(frame) &&
		    timevalisset(&pstats->p_timer[ITIMER_VIRTUAL].it_value) &&
		    itimerdecr(&pstats->p_timer[ITIMER_VIRTUAL], tick) == 0)
			psignal(p, SIGVTALRM);
		if (timevalisset(&pstats->p_timer[ITIMER_PROF].it_value) &&
		    itimerdecr(&pstats->p_timer[ITIMER_PROF], tick) == 0)
			psignal(p, SIGPROF);
		rel_mplock();
	}
	setdelayed();
}

/*
 * The statistics clock typically runs at a 125Hz rate, and is intended
 * to be frequency offset from the hardclock (typ 100Hz).  It is per-cpu.
 *
 * NOTE! systimer! the MP lock might not be held here.  We can only safely
 * manipulate objects owned by the current cpu.
 *
 * The stats clock is responsible for grabbing a profiling sample.
 * Most of the statistics are only used by user-level statistics programs.
 * The main exceptions are p->p_uticks, p->p_sticks, p->p_iticks, and
 * p->p_estcpu.
 *
 * Like the other clocks, the stat clock is called from what is effectively
 * a fast interrupt, so the context should be the thread/process that got
 * interrupted.
 */
static void
statclock(systimer_t info, struct intrframe *frame)
{
#ifdef GPROF
	struct gmonparam *g;
	int i;
#endif
	thread_t td;
	struct proc *p;
	int bump;
	struct timeval tv;
	struct timeval *stv;

	/*
	 * How big was our timeslice relative to the last time?
	 */
	microuptime(&tv);	/* mpsafe */
	stv = &mycpu->gd_stattv;
	if (stv->tv_sec == 0) {
	    bump = 1;
	} else {
	    bump = tv.tv_usec - stv->tv_usec +
		(tv.tv_sec - stv->tv_sec) * 1000000;
	    if (bump < 0)
		bump = 0;
	    if (bump > 1000000)
		bump = 1000000;
	}
	*stv = tv;

	td = curthread;
	p = td->td_proc;

	if (frame && CLKF_USERMODE(frame)) {
		/*
		 * Came from userland, handle user time and deal with
		 * possible process.
		 */
		if (p && (p->p_flag & P_PROFIL))
			addupc_intr(p, CLKF_PC(frame), 1);
		td->td_uticks += bump;

		/*
		 * Charge the time as appropriate
		 */
		if (p && p->p_nice > NZERO)
			cp_time[CP_NICE] += bump;
		else
			cp_time[CP_USER] += bump;
	} else {
#ifdef GPROF
		/*
		 * Kernel statistics are just like addupc_intr, only easier.
		 */
		g = &_gmonparam;
		if (g->state == GMON_PROF_ON && frame) {
			i = CLKF_PC(frame) - g->lowpc;
			if (i < g->textsize) {
				i /= HISTFRACTION * sizeof(*g->kcount);
				g->kcount[i]++;
			}
		}
#endif
		/*
		 * Came from kernel mode, so we were:
		 * - handling an interrupt,
		 * - doing syscall or trap work on behalf of the current
		 *   user process, or
		 * - spinning in the idle loop.
		 * Whichever it is, charge the time as appropriate.
		 * Note that we charge interrupts to the current process,
		 * regardless of whether they are ``for'' that process,
		 * so that we know how much of its real time was spent
		 * in ``non-process'' (i.e., interrupt) work.
		 *
		 * XXX assume system if frame is NULL.  A NULL frame 
		 * can occur if ipi processing is done from an splx().
		 */
		if (frame && CLKF_INTR(frame))
			td->td_iticks += bump;
		else
			td->td_sticks += bump;

		if (frame && CLKF_INTR(frame)) {
			cp_time[CP_INTR] += bump;
		} else {
			if (td == &mycpu->gd_idlethread)
				cp_time[CP_IDLE] += bump;
			else
				cp_time[CP_SYS] += bump;
		}
	}
}

/*
 * The scheduler clock typically runs at a 20Hz rate.  NOTE! systimer,
 * the MP lock might not be held.  We can safely manipulate parts of curproc
 * but that's about it.
 */
static void
schedclock(systimer_t info, struct intrframe *frame)
{
	struct proc *p;
	struct pstats *pstats;
	struct rusage *ru;
	struct vmspace *vm;
	long rss;

	schedulerclock(NULL);	/* mpsafe */
	if ((p = curproc) != NULL) {
		/* Update resource usage integrals and maximums. */
		if ((pstats = p->p_stats) != NULL &&
		    (ru = &pstats->p_ru) != NULL &&
		    (vm = p->p_vmspace) != NULL) {
			ru->ru_ixrss += pgtok(vm->vm_tsize);
			ru->ru_idrss += pgtok(vm->vm_dsize);
			ru->ru_isrss += pgtok(vm->vm_ssize);
			rss = pgtok(vmspace_resident_count(vm));
			if (ru->ru_maxrss < rss)
				ru->ru_maxrss = rss;
		}
	}
}

/*
 * Compute number of ticks for the specified amount of time.  The 
 * return value is intended to be used in a clock interrupt timed
 * operation and guarenteed to meet or exceed the requested time.
 * If the representation overflows, return INT_MAX.  The minimum return
 * value is 1 ticks and the function will average the calculation up.
 * If any value greater then 0 microseconds is supplied, a value
 * of at least 2 will be returned to ensure that a near-term clock
 * interrupt does not cause the timeout to occur (degenerately) early.
 *
 * Note that limit checks must take into account microseconds, which is
 * done simply by using the smaller signed long maximum instead of
 * the unsigned long maximum.
 *
 * If ints have 32 bits, then the maximum value for any timeout in
 * 10ms ticks is 248 days.
 */
int
tvtohz_high(struct timeval *tv)
{
	int ticks;
	long sec, usec;

	sec = tv->tv_sec;
	usec = tv->tv_usec;
	if (usec < 0) {
		sec--;
		usec += 1000000;
	}
	if (sec < 0) {
#ifdef DIAGNOSTIC
		if (usec > 0) {
			sec++;
			usec -= 1000000;
		}
		printf("tvotohz: negative time difference %ld sec %ld usec\n",
		       sec, usec);
#endif
		ticks = 1;
	} else if (sec <= INT_MAX / hz) {
		ticks = (int)(sec * hz + 
			    ((u_long)usec + (tick - 1)) / tick) + 1;
	} else {
		ticks = INT_MAX;
	}
	return (ticks);
}

/*
 * Compute number of ticks for the specified amount of time, erroring on
 * the side of it being too low to ensure that sleeping the returned number
 * of ticks will not result in a late return.
 *
 * The supplied timeval may not be negative and should be normalized.  A
 * return value of 0 is possible if the timeval converts to less then
 * 1 tick.
 *
 * If ints have 32 bits, then the maximum value for any timeout in
 * 10ms ticks is 248 days.
 */
int
tvtohz_low(struct timeval *tv)
{
	int ticks;
	long sec;

	sec = tv->tv_sec;
	if (sec <= INT_MAX / hz)
		ticks = (int)(sec * hz + (u_long)tv->tv_usec / tick);
	else
		ticks = INT_MAX;
	return (ticks);
}


/*
 * Start profiling on a process.
 *
 * Kernel profiling passes proc0 which never exits and hence
 * keeps the profile clock running constantly.
 */
void
startprofclock(struct proc *p)
{
	if ((p->p_flag & P_PROFIL) == 0) {
		p->p_flag |= P_PROFIL;
#if 0	/* XXX */
		if (++profprocs == 1 && stathz != 0) {
			s = splstatclock();
			psdiv = psratio;
			setstatclockrate(profhz);
			splx(s);
		}
#endif
	}
}

/*
 * Stop profiling on a process.
 */
void
stopprofclock(struct proc *p)
{
	if (p->p_flag & P_PROFIL) {
		p->p_flag &= ~P_PROFIL;
#if 0	/* XXX */
		if (--profprocs == 0 && stathz != 0) {
			s = splstatclock();
			psdiv = 1;
			setstatclockrate(stathz);
			splx(s);
		}
#endif
	}
}

/*
 * Return information about system clocks.
 */
static int
sysctl_kern_clockrate(SYSCTL_HANDLER_ARGS)
{
	struct clockinfo clkinfo;
	/*
	 * Construct clockinfo structure.
	 */
	clkinfo.hz = hz;
	clkinfo.tick = tick;
	clkinfo.tickadj = tickadj;
	clkinfo.profhz = profhz;
	clkinfo.stathz = stathz ? stathz : hz;
	return (sysctl_handle_opaque(oidp, &clkinfo, sizeof clkinfo, req));
}

SYSCTL_PROC(_kern, KERN_CLOCKRATE, clockrate, CTLTYPE_STRUCT|CTLFLAG_RD,
	0, 0, sysctl_kern_clockrate, "S,clockinfo","");

/*
 * We have eight functions for looking at the clock, four for
 * microseconds and four for nanoseconds.  For each there is fast
 * but less precise version "get{nano|micro}[up]time" which will
 * return a time which is up to 1/HZ previous to the call, whereas
 * the raw version "{nano|micro}[up]time" will return a timestamp
 * which is as precise as possible.  The "up" variants return the
 * time relative to system boot, these are well suited for time
 * interval measurements.
 *
 * Each cpu independantly maintains the current time of day, so all
 * we need to do to protect ourselves from changes is to do a loop
 * check on the seconds field changing out from under us.
 */
void
getmicrouptime(struct timeval *tvp)
{
	struct globaldata *gd = mycpu;
	sysclock_t delta;

	do {
		tvp->tv_sec = gd->gd_time_seconds;
		delta = gd->gd_hardclock.time - gd->gd_cpuclock_base;
	} while (tvp->tv_sec != gd->gd_time_seconds);
	tvp->tv_usec = (cputimer_freq64_usec * delta) >> 32;
	if (tvp->tv_usec >= 1000000) {
		tvp->tv_usec -= 1000000;
		++tvp->tv_sec;
	}
}

void
getnanouptime(struct timespec *tsp)
{
	struct globaldata *gd = mycpu;
	sysclock_t delta;

	do {
		tsp->tv_sec = gd->gd_time_seconds;
		delta = gd->gd_hardclock.time - gd->gd_cpuclock_base;
	} while (tsp->tv_sec != gd->gd_time_seconds);
	tsp->tv_nsec = (cputimer_freq64_nsec * delta) >> 32;
	if (tsp->tv_nsec >= 1000000000) {
		tsp->tv_nsec -= 1000000000;
		++tsp->tv_sec;
	}
}

void
microuptime(struct timeval *tvp)
{
	struct globaldata *gd = mycpu;
	sysclock_t delta;

	do {
		tvp->tv_sec = gd->gd_time_seconds;
		delta = cputimer_count() - gd->gd_cpuclock_base;
	} while (tvp->tv_sec != gd->gd_time_seconds);
	tvp->tv_usec = (cputimer_freq64_usec * delta) >> 32;
	if (tvp->tv_usec >= 1000000) {
		tvp->tv_usec -= 1000000;
		++tvp->tv_sec;
	}
}

void
nanouptime(struct timespec *tsp)
{
	struct globaldata *gd = mycpu;
	sysclock_t delta;

	do {
		tsp->tv_sec = gd->gd_time_seconds;
		delta = cputimer_count() - gd->gd_cpuclock_base;
	} while (tsp->tv_sec != gd->gd_time_seconds);
	tsp->tv_nsec = (cputimer_freq64_nsec * delta) >> 32;
	if (tsp->tv_nsec >= 1000000000) {
		tsp->tv_nsec -= 1000000000;
		++tsp->tv_sec;
	}
}

/*
 * realtime routines
 */

void
getmicrotime(struct timeval *tvp)
{
	struct globaldata *gd = mycpu;
	sysclock_t delta;

	do {
		tvp->tv_sec = gd->gd_time_seconds;
		delta = gd->gd_hardclock.time - gd->gd_cpuclock_base;
	} while (tvp->tv_sec != gd->gd_time_seconds);
	tvp->tv_usec = (cputimer_freq64_usec * delta) >> 32;

	tvp->tv_sec += basetime.tv_sec;
	tvp->tv_usec += basetime.tv_nsec / 1000;
	while (tvp->tv_usec >= 1000000) {
		tvp->tv_usec -= 1000000;
		++tvp->tv_sec;
	}
}

void
getnanotime(struct timespec *tsp)
{
	struct globaldata *gd = mycpu;
	sysclock_t delta;

	do {
		tsp->tv_sec = gd->gd_time_seconds;
		delta = gd->gd_hardclock.time - gd->gd_cpuclock_base;
	} while (tsp->tv_sec != gd->gd_time_seconds);
	tsp->tv_nsec = (cputimer_freq64_nsec * delta) >> 32;

	tsp->tv_sec += basetime.tv_sec;
	tsp->tv_nsec += basetime.tv_nsec;
	while (tsp->tv_nsec >= 1000000000) {
		tsp->tv_nsec -= 1000000000;
		++tsp->tv_sec;
	}
}

void
microtime(struct timeval *tvp)
{
	struct globaldata *gd = mycpu;
	sysclock_t delta;

	do {
		tvp->tv_sec = gd->gd_time_seconds;
		delta = cputimer_count() - gd->gd_cpuclock_base;
	} while (tvp->tv_sec != gd->gd_time_seconds);
	tvp->tv_usec = (cputimer_freq64_usec * delta) >> 32;

	tvp->tv_sec += basetime.tv_sec;
	tvp->tv_usec += basetime.tv_nsec / 1000;
	while (tvp->tv_usec >= 1000000) {
		tvp->tv_usec -= 1000000;
		++tvp->tv_sec;
	}
}

void
nanotime(struct timespec *tsp)
{
	struct globaldata *gd = mycpu;
	sysclock_t delta;

	do {
		tsp->tv_sec = gd->gd_time_seconds;
		delta = cputimer_count() - gd->gd_cpuclock_base;
	} while (tsp->tv_sec != gd->gd_time_seconds);
	tsp->tv_nsec = (cputimer_freq64_nsec * delta) >> 32;

	tsp->tv_sec += basetime.tv_sec;
	tsp->tv_nsec += basetime.tv_nsec;
	while (tsp->tv_nsec >= 1000000000) {
		tsp->tv_nsec -= 1000000000;
		++tsp->tv_sec;
	}
}

int
pps_ioctl(u_long cmd, caddr_t data, struct pps_state *pps)
{
	pps_params_t *app;
	struct pps_fetch_args *fapi;
#ifdef PPS_SYNC
	struct pps_kcbind_args *kapi;
#endif

	switch (cmd) {
	case PPS_IOC_CREATE:
		return (0);
	case PPS_IOC_DESTROY:
		return (0);
	case PPS_IOC_SETPARAMS:
		app = (pps_params_t *)data;
		if (app->mode & ~pps->ppscap)
			return (EINVAL);
		pps->ppsparam = *app;         
		return (0);
	case PPS_IOC_GETPARAMS:
		app = (pps_params_t *)data;
		*app = pps->ppsparam;
		app->api_version = PPS_API_VERS_1;
		return (0);
	case PPS_IOC_GETCAP:
		*(int*)data = pps->ppscap;
		return (0);
	case PPS_IOC_FETCH:
		fapi = (struct pps_fetch_args *)data;
		if (fapi->tsformat && fapi->tsformat != PPS_TSFMT_TSPEC)
			return (EINVAL);
		if (fapi->timeout.tv_sec || fapi->timeout.tv_nsec)
			return (EOPNOTSUPP);
		pps->ppsinfo.current_mode = pps->ppsparam.mode;         
		fapi->pps_info_buf = pps->ppsinfo;
		return (0);
	case PPS_IOC_KCBIND:
#ifdef PPS_SYNC
		kapi = (struct pps_kcbind_args *)data;
		/* XXX Only root should be able to do this */
		if (kapi->tsformat && kapi->tsformat != PPS_TSFMT_TSPEC)
			return (EINVAL);
		if (kapi->kernel_consumer != PPS_KC_HARDPPS)
			return (EINVAL);
		if (kapi->edge & ~pps->ppscap)
			return (EINVAL);
		pps->kcmode = kapi->edge;
		return (0);
#else
		return (EOPNOTSUPP);
#endif
	default:
		return (ENOTTY);
	}
}

void
pps_init(struct pps_state *pps)
{
	pps->ppscap |= PPS_TSFMT_TSPEC;
	if (pps->ppscap & PPS_CAPTUREASSERT)
		pps->ppscap |= PPS_OFFSETASSERT;
	if (pps->ppscap & PPS_CAPTURECLEAR)
		pps->ppscap |= PPS_OFFSETCLEAR;
}

void
pps_event(struct pps_state *pps, sysclock_t count, int event)
{
	struct globaldata *gd;
	struct timespec *tsp;
	struct timespec *osp;
	struct timespec ts;
	sysclock_t *pcount;
#ifdef PPS_SYNC
	sysclock_t tcount;
#endif
	sysclock_t delta;
	pps_seq_t *pseq;
	int foff;
	int fhard;

	gd = mycpu;

	/* Things would be easier with arrays... */
	if (event == PPS_CAPTUREASSERT) {
		tsp = &pps->ppsinfo.assert_timestamp;
		osp = &pps->ppsparam.assert_offset;
		foff = pps->ppsparam.mode & PPS_OFFSETASSERT;
		fhard = pps->kcmode & PPS_CAPTUREASSERT;
		pcount = &pps->ppscount[0];
		pseq = &pps->ppsinfo.assert_sequence;
	} else {
		tsp = &pps->ppsinfo.clear_timestamp;
		osp = &pps->ppsparam.clear_offset;
		foff = pps->ppsparam.mode & PPS_OFFSETCLEAR;
		fhard = pps->kcmode & PPS_CAPTURECLEAR;
		pcount = &pps->ppscount[1];
		pseq = &pps->ppsinfo.clear_sequence;
	}

	/* Nothing really happened */
	if (*pcount == count)
		return;

	*pcount = count;

	do {
		ts.tv_sec = gd->gd_time_seconds;
		delta = count - gd->gd_cpuclock_base;
	} while (ts.tv_sec != gd->gd_time_seconds);
	if (delta > cputimer_freq) {
		ts.tv_sec += delta / cputimer_freq;
		delta %= cputimer_freq;
	}
	ts.tv_nsec = (cputimer_freq64_nsec * delta) >> 32;
	ts.tv_sec += basetime.tv_sec;
	ts.tv_nsec += basetime.tv_nsec;
	while (ts.tv_nsec >= 1000000000) {
		ts.tv_nsec -= 1000000000;
		++ts.tv_sec;
	}

	(*pseq)++;
	*tsp = ts;

	if (foff) {
		timespecadd(tsp, osp);
		if (tsp->tv_nsec < 0) {
			tsp->tv_nsec += 1000000000;
			tsp->tv_sec -= 1;
		}
	}
#ifdef PPS_SYNC
	if (fhard) {
		/* magic, at its best... */
		tcount = count - pps->ppscount[2];
		pps->ppscount[2] = count;
		delta = (cputimer_freq64_nsec * tcount) >> 32;
		hardpps(tsp, delta);
	}
#endif
}

