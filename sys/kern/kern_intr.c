/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com> All rights reserved.
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org> All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/kern_intr.c,v 1.24.2.1 2001/10/14 20:05:50 luigi Exp $
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/random.h>
#include <sys/serialize.h>
#include <sys/interrupt.h>
#include <sys/bus.h>
#include <sys/machintr.h>

#include <machine/frame.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>

struct intr_info;

typedef struct intrec {
    struct intrec *next;
    struct intr_info *info;
    inthand2_t	*handler;
    void	*argument;
    char	*name;
    int		intr;
    int		intr_flags;
    struct lwkt_serialize *serializer;
} *intrec_t;

struct intr_info {
	intrec_t	i_reclist;
	struct thread	*i_thread;	/* don't embed struct thread */
	struct random_softc i_random;
	int		i_running;
	long		i_count;	/* interrupts dispatched */
	int		i_mplock_required;
	int		i_fast;
	int		i_slow;
	int		i_state;
	int		i_errorticks;
	unsigned long	i_straycount;
	int		i_cpuid;
	int		i_intr;
};

static struct intr_info intr_info_ary[MAXCPU][MAX_INTS];
static struct intr_info *swi_info_ary[MAX_SOFTINTS];

static int max_installed_hard_intr[MAXCPU];

#define EMERGENCY_INTR_POLLING_FREQ_MAX 20000

/*
 * Assert that callers into interrupt handlers don't return with
 * dangling tokens, spinlocks, or mp locks.
 */
#ifdef INVARIANTS

#define TD_INVARIANTS_DECLARE   \
        int spincount;          \
        lwkt_tokref_t curstop

#define TD_INVARIANTS_GET(td)                                   \
        do {                                                    \
                spincount = (td)->td_gd->gd_spinlocks;		\
                curstop = (td)->td_toks_stop;                   \
        } while(0)

#define TD_INVARIANTS_TEST(td, name)                                    \
        do {                                                            \
                KASSERT(spincount == (td)->td_gd->gd_spinlocks,		\
                        ("spincount mismatch after interrupt handler %s", \
                        name));                                         \
                KASSERT(curstop == (td)->td_toks_stop,                  \
                        ("token count mismatch after interrupt handler %s", \
                        name));                                         \
        } while(0)

#else

/* !INVARIANTS */

#define TD_INVARIANTS_DECLARE
#define TD_INVARIANTS_GET(td)
#define TD_INVARIANTS_TEST(td, name)

#endif /* ndef INVARIANTS */

static int sysctl_emergency_freq(SYSCTL_HANDLER_ARGS);
static int sysctl_emergency_enable(SYSCTL_HANDLER_ARGS);
static void emergency_intr_timer_callback(systimer_t, int, struct intrframe *);
static void ithread_handler(void *arg);
static void ithread_emergency(void *arg);
static void report_stray_interrupt(struct intr_info *info, const char *func);
static void int_moveto_destcpu(int *, int);
static void int_moveto_origcpu(int, int);
static void sched_ithd_intern(struct intr_info *info);

static struct systimer emergency_intr_timer[MAXCPU];
static struct thread emergency_intr_thread[MAXCPU];

#define ISTATE_NOTHREAD		0
#define ISTATE_NORMAL		1
#define ISTATE_LIVELOCKED	2

static int livelock_limit = 40000;
static int livelock_lowater = 20000;
static int livelock_debug = -1;
SYSCTL_INT(_kern, OID_AUTO, livelock_limit,
        CTLFLAG_RW, &livelock_limit, 0, "Livelock interrupt rate limit");
SYSCTL_INT(_kern, OID_AUTO, livelock_lowater,
        CTLFLAG_RW, &livelock_lowater, 0, "Livelock low-water mark restore");
SYSCTL_INT(_kern, OID_AUTO, livelock_debug,
        CTLFLAG_RW, &livelock_debug, 0, "Livelock debug intr#");

static int emergency_intr_enable = 0;	/* emergency interrupt polling */
TUNABLE_INT("kern.emergency_intr_enable", &emergency_intr_enable);
SYSCTL_PROC(_kern, OID_AUTO, emergency_intr_enable, CTLTYPE_INT | CTLFLAG_RW,
        0, 0, sysctl_emergency_enable, "I", "Emergency Interrupt Poll Enable");

static int emergency_intr_freq = 10;	/* emergency polling frequency */
TUNABLE_INT("kern.emergency_intr_freq", &emergency_intr_freq);
SYSCTL_PROC(_kern, OID_AUTO, emergency_intr_freq, CTLTYPE_INT | CTLFLAG_RW,
        0, 0, sysctl_emergency_freq, "I", "Emergency Interrupt Poll Frequency");

/*
 * Sysctl support routines
 */
static int
sysctl_emergency_enable(SYSCTL_HANDLER_ARGS)
{
	int error, enabled, cpuid, freq;

	enabled = emergency_intr_enable;
	error = sysctl_handle_int(oidp, &enabled, 0, req);
	if (error || req->newptr == NULL)
		return error;
	emergency_intr_enable = enabled;
	if (emergency_intr_enable)
		freq = emergency_intr_freq;
	else
		freq = 1;

	for (cpuid = 0; cpuid < ncpus; ++cpuid)
		systimer_adjust_periodic(&emergency_intr_timer[cpuid], freq);
	return 0;
}

static int
sysctl_emergency_freq(SYSCTL_HANDLER_ARGS)
{
        int error, phz, cpuid, freq;

        phz = emergency_intr_freq;
        error = sysctl_handle_int(oidp, &phz, 0, req);
        if (error || req->newptr == NULL)
                return error;
        if (phz <= 0)
                return EINVAL;
        else if (phz > EMERGENCY_INTR_POLLING_FREQ_MAX)
                phz = EMERGENCY_INTR_POLLING_FREQ_MAX;

        emergency_intr_freq = phz;
	if (emergency_intr_enable)
		freq = emergency_intr_freq;
	else
		freq = 1;

	for (cpuid = 0; cpuid < ncpus; ++cpuid)
		systimer_adjust_periodic(&emergency_intr_timer[cpuid], freq);
        return 0;
}

/*
 * Register an SWI or INTerrupt handler.
 */
void *
register_swi(int intr, inthand2_t *handler, void *arg, const char *name,
		struct lwkt_serialize *serializer, int cpuid)
{
    if (intr < FIRST_SOFTINT || intr >= MAX_INTS)
	panic("register_swi: bad intr %d", intr);

    if (cpuid < 0)
	cpuid = intr % ncpus;
    return(register_int(intr, handler, arg, name, serializer, 0, cpuid));
}

void *
register_swi_mp(int intr, inthand2_t *handler, void *arg, const char *name,
		struct lwkt_serialize *serializer, int cpuid)
{
    if (intr < FIRST_SOFTINT || intr >= MAX_INTS)
	panic("register_swi: bad intr %d", intr);

    if (cpuid < 0)
	cpuid = intr % ncpus;
    return(register_int(intr, handler, arg, name, serializer,
        INTR_MPSAFE, cpuid));
}

void *
register_int(int intr, inthand2_t *handler, void *arg, const char *name,
		struct lwkt_serialize *serializer, int intr_flags, int cpuid)
{
    struct intr_info *info;
    struct intrec **list;
    intrec_t rec;
    int orig_cpuid;

    KKASSERT(cpuid >= 0 && cpuid < ncpus);

    if (intr < 0 || intr >= MAX_INTS)
	panic("register_int: bad intr %d", intr);
    if (name == NULL)
	name = "???";
    info = &intr_info_ary[cpuid][intr];

    /*
     * Construct an interrupt handler record
     */
    rec = kmalloc(sizeof(struct intrec), M_DEVBUF, M_INTWAIT);
    rec->name = kmalloc(strlen(name) + 1, M_DEVBUF, M_INTWAIT);
    strcpy(rec->name, name);

    rec->info = info;
    rec->handler = handler;
    rec->argument = arg;
    rec->intr = intr;
    rec->intr_flags = intr_flags;
    rec->next = NULL;
    rec->serializer = serializer;

    int_moveto_destcpu(&orig_cpuid, cpuid);

    /*
     * Create an emergency polling thread and set up a systimer to wake
     * it up.
     */
    if (emergency_intr_thread[cpuid].td_kstack == NULL) {
	lwkt_create(ithread_emergency, NULL, NULL,
		    &emergency_intr_thread[cpuid],
		    TDF_NOSTART | TDF_INTTHREAD, cpuid, "ithreadE %d",
		    cpuid);
	systimer_init_periodic_nq(&emergency_intr_timer[cpuid],
		    emergency_intr_timer_callback,
		    &emergency_intr_thread[cpuid],
		    (emergency_intr_enable ? emergency_intr_freq : 1));
    }

    /*
     * Create an interrupt thread if necessary, leave it in an unscheduled
     * state.
     */
    if (info->i_state == ISTATE_NOTHREAD) {
	info->i_state = ISTATE_NORMAL;
	info->i_thread = kmalloc(sizeof(struct thread), M_DEVBUF,
	    M_INTWAIT | M_ZERO);
	lwkt_create(ithread_handler, (void *)(intptr_t)intr, NULL,
		    info->i_thread, TDF_NOSTART | TDF_INTTHREAD, cpuid,
		    "ithread%d %d", intr, cpuid);
	if (intr >= FIRST_SOFTINT)
	    lwkt_setpri(info->i_thread, TDPRI_SOFT_NORM);
	else
	    lwkt_setpri(info->i_thread, TDPRI_INT_MED);
	info->i_thread->td_preemptable = lwkt_preempt;
    }

    list = &info->i_reclist;

    /*
     * Keep track of how many fast and slow interrupts we have.
     * Set i_mplock_required if any handler in the chain requires
     * the MP lock to operate.
     */
    if ((intr_flags & INTR_MPSAFE) == 0)
	info->i_mplock_required = 1;
    if (intr_flags & INTR_CLOCK)
	++info->i_fast;
    else
	++info->i_slow;

    /*
     * Enable random number generation keying off of this interrupt.
     */
    if ((intr_flags & INTR_NOENTROPY) == 0 && info->i_random.sc_enabled == 0) {
	info->i_random.sc_enabled = 1;
	info->i_random.sc_intr = intr;
    }

    /*
     * Add the record to the interrupt list.
     */
    crit_enter();
    while (*list != NULL)
	list = &(*list)->next;
    *list = rec;
    crit_exit();

    /*
     * Update max_installed_hard_intr to make the emergency intr poll
     * a bit more efficient.
     */
    if (intr < FIRST_SOFTINT) {
	if (max_installed_hard_intr[cpuid] <= intr)
	    max_installed_hard_intr[cpuid] = intr + 1;
    }

    if (intr >= FIRST_SOFTINT)
	swi_info_ary[intr - FIRST_SOFTINT] = info;

    /*
     * Setup the machine level interrupt vector
     */
    if (intr < FIRST_SOFTINT && info->i_slow + info->i_fast == 1)
	machintr_intr_setup(intr, intr_flags);

    int_moveto_origcpu(orig_cpuid, cpuid);

    return(rec);
}

void
unregister_swi(void *id, int intr, int cpuid)
{
    if (cpuid < 0)
	cpuid = intr % ncpus;

    unregister_int(id, cpuid);
}

void
unregister_int(void *id, int cpuid)
{
    struct intr_info *info;
    struct intrec **list;
    intrec_t rec;
    int intr, orig_cpuid;

    KKASSERT(cpuid >= 0 && cpuid < ncpus);

    intr = ((intrec_t)id)->intr;

    if (intr < 0 || intr >= MAX_INTS)
	panic("register_int: bad intr %d", intr);

    info = &intr_info_ary[cpuid][intr];

    int_moveto_destcpu(&orig_cpuid, cpuid);

    /*
     * Remove the interrupt descriptor, adjust the descriptor count,
     * and teardown the machine level vector if this was the last interrupt.
     */
    crit_enter();
    list = &info->i_reclist;
    while ((rec = *list) != NULL) {
	if (rec == id)
	    break;
	list = &rec->next;
    }
    if (rec) {
	intrec_t rec0;

	*list = rec->next;
	if (rec->intr_flags & INTR_CLOCK)
	    --info->i_fast;
	else
	    --info->i_slow;
	if (intr < FIRST_SOFTINT && info->i_fast + info->i_slow == 0)
	    machintr_intr_teardown(intr);

	/*
	 * Clear i_mplock_required if no handlers in the chain require the
	 * MP lock.
	 */
	for (rec0 = info->i_reclist; rec0; rec0 = rec0->next) {
	    if ((rec0->intr_flags & INTR_MPSAFE) == 0)
		break;
	}
	if (rec0 == NULL)
	    info->i_mplock_required = 0;
    }

    if (intr >= FIRST_SOFTINT && info->i_reclist == NULL)
	swi_info_ary[intr - FIRST_SOFTINT] = NULL;

    crit_exit();

    int_moveto_origcpu(orig_cpuid, cpuid);

    /*
     * Free the record.
     */
    if (rec != NULL) {
	kfree(rec->name, M_DEVBUF);
	kfree(rec, M_DEVBUF);
    } else {
	kprintf("warning: unregister_int: int %d handler for %s not found\n",
		intr, ((intrec_t)id)->name);
    }
}

long
get_interrupt_counter(int intr, int cpuid)
{
    struct intr_info *info;

    KKASSERT(cpuid >= 0 && cpuid < ncpus);

    if (intr < 0 || intr >= MAX_INTS)
	panic("register_int: bad intr %d", intr);
    info = &intr_info_ary[cpuid][intr];
    return(info->i_count);
}

void
register_randintr(int intr)
{
    struct intr_info *info;
    int cpuid;

    if (intr < 0 || intr >= MAX_INTS)
	panic("register_randintr: bad intr %d", intr);

    for (cpuid = 0; cpuid < ncpus; ++cpuid) {
	info = &intr_info_ary[cpuid][intr];
	info->i_random.sc_intr = intr;
	info->i_random.sc_enabled = 1;
    }
}

void
unregister_randintr(int intr)
{
    struct intr_info *info;
    int cpuid;

    if (intr < 0 || intr >= MAX_INTS)
	panic("register_swi: bad intr %d", intr);

    for (cpuid = 0; cpuid < ncpus; ++cpuid) {
	info = &intr_info_ary[cpuid][intr];
	info->i_random.sc_enabled = -1;
    }
}

int
next_registered_randintr(int intr)
{
    struct intr_info *info;

    if (intr < 0 || intr >= MAX_INTS)
	panic("register_swi: bad intr %d", intr);

    while (intr < MAX_INTS) {
	int cpuid;

	for (cpuid = 0; cpuid < ncpus; ++cpuid) {
	    info = &intr_info_ary[cpuid][intr];
	    if (info->i_random.sc_enabled > 0)
		return intr;
	}
	++intr;
    }
    return intr;
}

/*
 * Dispatch an interrupt.  If there's nothing to do we have a stray
 * interrupt and can just return, leaving the interrupt masked.
 *
 * We need to schedule the interrupt and set its i_running bit.  If
 * we are not on the interrupt thread's cpu we have to send a message
 * to the correct cpu that will issue the desired action (interlocking
 * with the interrupt thread's critical section).  We do NOT attempt to
 * reschedule interrupts whos i_running bit is already set because
 * this would prematurely wakeup a livelock-limited interrupt thread.
 *
 * i_running is only tested/set on the same cpu as the interrupt thread.
 *
 * We are NOT in a critical section, which will allow the scheduled
 * interrupt to preempt us.  The MP lock might *NOT* be held here.
 */
static void
sched_ithd_remote(void *arg)
{
    sched_ithd_intern(arg);
}

static void
sched_ithd_intern(struct intr_info *info)
{
    ++info->i_count;
    if (info->i_state != ISTATE_NOTHREAD) {
	if (info->i_reclist == NULL) {
	    report_stray_interrupt(info, "sched_ithd");
	} else {
	    if (info->i_thread->td_gd == mycpu) {
		if (info->i_running == 0) {
		    info->i_running = 1;
		    if (info->i_state != ISTATE_LIVELOCKED)
			lwkt_schedule(info->i_thread); /* MIGHT PREEMPT */
		}
	    } else {
		lwkt_send_ipiq(info->i_thread->td_gd, sched_ithd_remote, info);
	    }
	}
    } else {
	report_stray_interrupt(info, "sched_ithd");
    }
}

void
sched_ithd_soft(int intr)
{
	struct intr_info *info;

	KKASSERT(intr >= FIRST_SOFTINT && intr < MAX_INTS);

	info = swi_info_ary[intr - FIRST_SOFTINT];
	if (info != NULL) {
		sched_ithd_intern(info);
	} else {
		kprintf("unregistered softint %d got scheduled on cpu%d\n",
		    intr, mycpuid);
	}
}

void
sched_ithd_hard(int intr)
{
	KKASSERT(intr >= 0 && intr < MAX_HARDINTS);
	sched_ithd_intern(&intr_info_ary[mycpuid][intr]);
}

#ifdef _KERNEL_VIRTUAL

void
sched_ithd_hard_virtual(int intr)
{
	KKASSERT(intr >= 0 && intr < MAX_HARDINTS);
	sched_ithd_intern(&intr_info_ary[0][intr]);
}

void *
register_int_virtual(int intr, inthand2_t *handler, void *arg, const char *name,
    struct lwkt_serialize *serializer, int intr_flags)
{
	return register_int(intr, handler, arg, name, serializer, intr_flags, 0);
}

void
unregister_int_virtual(void *id)
{
	unregister_int(id, 0);
}

#endif	/* _KERN_VIRTUAL */

static void
report_stray_interrupt(struct intr_info *info, const char *func)
{
	++info->i_straycount;
	if (info->i_straycount < 10) {
		if (info->i_errorticks == ticks)
			return;
		info->i_errorticks = ticks;
		kprintf("%s: stray interrupt %d on cpu%d\n",
		    func, info->i_intr, mycpuid);
	} else if (info->i_straycount == 10) {
		kprintf("%s: %ld stray interrupts %d on cpu%d - "
			"there will be no further reports\n", func,
			info->i_straycount, info->i_intr, mycpuid);
	}
}

/*
 * This is run from a periodic SYSTIMER (and thus must be MP safe, the BGL
 * might not be held).
 */
static void
ithread_livelock_wakeup(systimer_t st, int in_ipi __unused,
    struct intrframe *frame __unused)
{
    struct intr_info *info;

    info = &intr_info_ary[mycpuid][(int)(intptr_t)st->data];
    if (info->i_state != ISTATE_NOTHREAD)
	lwkt_schedule(info->i_thread);
}

/*
 * Schedule ithread within fast intr handler
 *
 * XXX Protect sched_ithd_hard() call with gd_intr_nesting_level?
 * Interrupts aren't enabled, but still...
 */
static __inline void
ithread_fast_sched(int intr, thread_t td)
{
    ++td->td_nest_count;

    /*
     * We are already in critical section, exit it now to
     * allow preemption.
     */
    crit_exit_quick(td);
    sched_ithd_hard(intr);
    crit_enter_quick(td);

    --td->td_nest_count;
}

/*
 * This function is called directly from the ICU or APIC vector code assembly
 * to process an interrupt.  The critical section and interrupt deferral
 * checks have already been done but the function is entered WITHOUT
 * a critical section held.  The BGL may or may not be held.
 *
 * Must return non-zero if we do not want the vector code to re-enable
 * the interrupt (which we don't if we have to schedule the interrupt)
 */
int ithread_fast_handler(struct intrframe *frame);

int
ithread_fast_handler(struct intrframe *frame)
{
    int intr;
    struct intr_info *info;
    struct intrec **list;
    int must_schedule;
    int got_mplock;
    TD_INVARIANTS_DECLARE;
    intrec_t rec, nrec;
    globaldata_t gd;
    thread_t td;

    intr = frame->if_vec;
    gd = mycpu;
    td = curthread;

    /* We must be in critical section. */
    KKASSERT(td->td_critcount);

    info = &intr_info_ary[mycpuid][intr];

    /*
     * If we are not processing any FAST interrupts, just schedule the thing.
     */
    if (info->i_fast == 0) {
    	++gd->gd_cnt.v_intr;
	ithread_fast_sched(intr, td);
	return(1);
    }

    /*
     * This should not normally occur since interrupts ought to be 
     * masked if the ithread has been scheduled or is running.
     */
    if (info->i_running)
	return(1);

    /*
     * Bump the interrupt nesting level to process any FAST interrupts.
     * Obtain the MP lock as necessary.  If the MP lock cannot be obtained,
     * schedule the interrupt thread to deal with the issue instead.
     *
     * To reduce overhead, just leave the MP lock held once it has been
     * obtained.
     */
    ++gd->gd_intr_nesting_level;
    ++gd->gd_cnt.v_intr;
    must_schedule = info->i_slow;
    got_mplock = 0;

    TD_INVARIANTS_GET(td);
    list = &info->i_reclist;

    for (rec = *list; rec; rec = nrec) {
	/* rec may be invalid after call */
	nrec = rec->next;

	if (rec->intr_flags & INTR_CLOCK) {
	    if ((rec->intr_flags & INTR_MPSAFE) == 0 && got_mplock == 0) {
		if (try_mplock() == 0) {
		    /* Couldn't get the MP lock; just schedule it. */
		    must_schedule = 1;
		    break;
		}
		got_mplock = 1;
	    }
	    if (rec->serializer) {
		must_schedule += lwkt_serialize_handler_try(
					rec->serializer, rec->handler,
					rec->argument, frame);
	    } else {
		rec->handler(rec->argument, frame);
	    }
	    TD_INVARIANTS_TEST(td, rec->name);
	}
    }

    /*
     * Cleanup
     */
    --gd->gd_intr_nesting_level;
    if (got_mplock)
	rel_mplock();

    /*
     * If we had a problem, or mixed fast and slow interrupt handlers are
     * registered, schedule the ithread to catch the missed records (it
     * will just re-run all of them).  A return value of 0 indicates that
     * all handlers have been run and the interrupt can be re-enabled, and
     * a non-zero return indicates that the interrupt thread controls
     * re-enablement.
     */
    if (must_schedule > 0)
	ithread_fast_sched(intr, td);
    else if (must_schedule == 0)
	++info->i_count;
    return(must_schedule);
}

/*
 * Interrupt threads run this as their main loop.
 *
 * The handler begins execution outside a critical section and no MP lock.
 *
 * The i_running state starts at 0.  When an interrupt occurs, the hardware
 * interrupt is disabled and sched_ithd_hard().  The HW interrupt remains
 * disabled until all routines have run.  We then call machintr_intr_enable()
 * to reenable the HW interrupt and deschedule us until the next interrupt. 
 *
 * We are responsible for atomically checking i_running.  i_running for our
 * irq is only set in the context of our cpu, so a critical section is a
 * sufficient interlock.
 */
#define LIVELOCK_TIMEFRAME(freq)	((freq) >> 2)	/* 1/4 second */

static void
ithread_handler(void *arg)
{
    struct intr_info *info;
    int use_limit;
    uint32_t lseconds;
    int intr, cpuid = mycpuid;
    int mpheld;
    struct intrec **list;
    intrec_t rec, nrec;
    globaldata_t gd;
    struct systimer ill_timer;	/* enforced freq. timer */
    u_int ill_count;		/* interrupt livelock counter */
    TD_INVARIANTS_DECLARE;

    ill_count = 0;
    intr = (int)(intptr_t)arg;
    info = &intr_info_ary[cpuid][intr];
    list = &info->i_reclist;

    /*
     * The loop must be entered with one critical section held.  The thread
     * does not hold the mplock on startup.
     */
    gd = mycpu;
    lseconds = gd->gd_time_seconds;
    crit_enter_gd(gd);
    mpheld = 0;

    for (;;) {
	/*
	 * The chain is only considered MPSAFE if all its interrupt handlers
	 * are MPSAFE.  However, if intr_mpsafe has been turned off we
	 * always operate with the BGL.
	 */
	if (info->i_mplock_required != mpheld) {
	    if (info->i_mplock_required) {
		KKASSERT(mpheld == 0);
		get_mplock();
		mpheld = 1;
	    } else {
		KKASSERT(mpheld != 0);
		rel_mplock();
		mpheld = 0;
	    }
	}

	TD_INVARIANTS_GET(gd->gd_curthread);

	/*
	 * If an interrupt is pending, clear i_running and execute the
	 * handlers.  Note that certain types of interrupts can re-trigger
	 * and set i_running again.
	 *
	 * Each handler is run in a critical section.  Note that we run both
	 * FAST and SLOW designated service routines.
	 */
	if (info->i_running) {
	    ++ill_count;
	    info->i_running = 0;

	    if (*list == NULL)
		report_stray_interrupt(info, "ithread_handler");

	    for (rec = *list; rec; rec = nrec) {
		/* rec may be invalid after call */
		nrec = rec->next;
		if (rec->serializer) {
		    lwkt_serialize_handler_call(rec->serializer, rec->handler,
						rec->argument, NULL);
		} else {
		    rec->handler(rec->argument, NULL);
		}
		TD_INVARIANTS_TEST(gd->gd_curthread, rec->name);
	    }
	}

	/*
	 * This is our interrupt hook to add rate randomness to the random
	 * number generator.
	 */
	if (info->i_random.sc_enabled > 0)
	    add_interrupt_randomness(intr);

	/*
	 * Unmask the interrupt to allow it to trigger again.  This only
	 * applies to certain types of interrupts (typ level interrupts).
	 * This can result in the interrupt retriggering, but the retrigger
	 * will not be processed until we cycle our critical section.
	 *
	 * Only unmask interrupts while handlers are installed.  It is
	 * possible to hit a situation where no handlers are installed
	 * due to a device driver livelocking and then tearing down its
	 * interrupt on close (the parallel bus being a good example).
	 */
	if (intr < FIRST_SOFTINT && *list)
	    machintr_intr_enable(intr);

	/*
	 * Do a quick exit/enter to catch any higher-priority interrupt
	 * sources, such as the statclock, so thread time accounting
	 * will still work.  This may also cause an interrupt to re-trigger.
	 */
	crit_exit_gd(gd);
	crit_enter_gd(gd);

	/*
	 * LIVELOCK STATE MACHINE
	 */
	switch(info->i_state) {
	case ISTATE_NORMAL:
	    /*
	     * Reset the count each second.
	     */
	    if (lseconds != gd->gd_time_seconds) {
		lseconds = gd->gd_time_seconds;
		ill_count = 0;
	    }

	    /*
	     * If we did not exceed the frequency limit, we are done.  
	     * If the interrupt has not retriggered we deschedule ourselves.
	     */
	    if (ill_count <= livelock_limit) {
		if (info->i_running == 0) {
		    lwkt_deschedule_self(gd->gd_curthread);
		    lwkt_switch();
		}
		break;
	    }

	    /*
	     * Otherwise we are livelocked.  Set up a periodic systimer
	     * to wake the thread up at the limit frequency.
	     */
	    kprintf("intr %d on cpu%d at %d/%d hz, livelocked limit engaged!\n",
		    intr, cpuid, ill_count, livelock_limit);
	    info->i_state = ISTATE_LIVELOCKED;
	    if ((use_limit = livelock_limit) < 100)
		use_limit = 100;
	    else if (use_limit > 500000)
		use_limit = 500000;
	    systimer_init_periodic_nq(&ill_timer, ithread_livelock_wakeup,
				      (void *)(intptr_t)intr, use_limit);
	    /* fall through */
	case ISTATE_LIVELOCKED:
	    /*
	     * Wait for our periodic timer to go off.  Since the interrupt
	     * has re-armed it can still set i_running, but it will not
	     * reschedule us while we are in a livelocked state.
	     */
	    lwkt_deschedule_self(gd->gd_curthread);
	    lwkt_switch();

	    /*
	     * Check once a second to see if the livelock condition no
	     * longer applies.
	     */
	    if (lseconds != gd->gd_time_seconds) {
		lseconds = gd->gd_time_seconds;
		if (ill_count < livelock_lowater) {
		    info->i_state = ISTATE_NORMAL;
		    systimer_del(&ill_timer);
		    kprintf("intr %d on cpu%d at %d/%d hz, livelock removed\n",
			    intr, cpuid, ill_count, livelock_lowater);
		} else if (livelock_debug == intr ||
			   (bootverbose && cold)) {
		    kprintf("intr %d on cpu%d at %d/%d hz, in livelock\n",
			    intr, cpuid, ill_count, livelock_lowater);
		}
		ill_count = 0;
	    }
	    break;
	}
    }
    /* NOT REACHED */
}

/*
 * Emergency interrupt polling thread.  The thread begins execution
 * outside a critical section with the BGL held.
 *
 * If emergency interrupt polling is enabled, this thread will 
 * execute all system interrupts not marked INTR_NOPOLL at the
 * specified polling frequency.
 *
 * WARNING!  This thread runs *ALL* interrupt service routines that
 * are not marked INTR_NOPOLL, which basically means everything except
 * the 8254 clock interrupt and the ATA interrupt.  It has very high
 * overhead and should only be used in situations where the machine
 * cannot otherwise be made to work.  Due to the severe performance
 * degredation, it should not be enabled on production machines.
 */
static void
ithread_emergency(void *arg __unused)
{
    globaldata_t gd = mycpu;
    struct intr_info *info;
    intrec_t rec, nrec;
    int intr, cpuid = mycpuid;
    TD_INVARIANTS_DECLARE;

    get_mplock();
    crit_enter_gd(gd);
    TD_INVARIANTS_GET(gd->gd_curthread);

    for (;;) {
	for (intr = 0; intr < max_installed_hard_intr[cpuid]; ++intr) {
	    info = &intr_info_ary[cpuid][intr];
	    for (rec = info->i_reclist; rec; rec = nrec) {
		/* rec may be invalid after call */
		nrec = rec->next;
		if ((rec->intr_flags & INTR_NOPOLL) == 0) {
		    if (rec->serializer) {
			lwkt_serialize_handler_try(rec->serializer,
						rec->handler, rec->argument, NULL);
		    } else {
			rec->handler(rec->argument, NULL);
		    }
		    TD_INVARIANTS_TEST(gd->gd_curthread, rec->name);
		}
	    }
	}
	lwkt_deschedule_self(gd->gd_curthread);
	lwkt_switch();
    }
    /* NOT REACHED */
}

/*
 * Systimer callback - schedule the emergency interrupt poll thread
 * 		       if emergency polling is enabled.
 */
static
void
emergency_intr_timer_callback(systimer_t info, int in_ipi __unused,
    struct intrframe *frame __unused)
{
    if (emergency_intr_enable)
	lwkt_schedule(info->data);
}

/* 
 * Sysctls used by systat and others: hw.intrnames and hw.intrcnt.
 * The data for this machine dependent, and the declarations are in machine
 * dependent code.  The layout of intrnames and intrcnt however is machine
 * independent.
 *
 * We do not know the length of intrcnt and intrnames at compile time, so
 * calculate things at run time.
 */

static int
sysctl_intrnames(SYSCTL_HANDLER_ARGS)
{
    struct intr_info *info;
    intrec_t rec;
    int error = 0;
    int len;
    int intr, cpuid;
    char buf[64];

    for (cpuid = 0; cpuid < ncpus; ++cpuid) {
	for (intr = 0; error == 0 && intr < MAX_INTS; ++intr) {
	    info = &intr_info_ary[cpuid][intr];

	    len = 0;
	    buf[0] = 0;
	    for (rec = info->i_reclist; rec; rec = rec->next) {
		ksnprintf(buf + len, sizeof(buf) - len, "%s%s",
		    (len ? "/" : ""), rec->name);
		len += strlen(buf + len);
	    }
	    if (len == 0) {
		ksnprintf(buf, sizeof(buf), "irq%d", intr);
		len = strlen(buf);
	    }
	    error = SYSCTL_OUT(req, buf, len + 1);
	}
    }
    return (error);
}

SYSCTL_PROC(_hw, OID_AUTO, intrnames, CTLTYPE_OPAQUE | CTLFLAG_RD,
	NULL, 0, sysctl_intrnames, "", "Interrupt Names");

static int
sysctl_intrcnt_all(SYSCTL_HANDLER_ARGS)
{
    struct intr_info *info;
    int error = 0;
    int intr, cpuid;

    for (cpuid = 0; cpuid < ncpus; ++cpuid) {
	for (intr = 0; intr < MAX_INTS; ++intr) {
	    info = &intr_info_ary[cpuid][intr];

	    error = SYSCTL_OUT(req, &info->i_count, sizeof(info->i_count));
	    if (error)
		goto failed;
	}
    }
failed:
    return(error);
}

SYSCTL_PROC(_hw, OID_AUTO, intrcnt_all, CTLTYPE_OPAQUE | CTLFLAG_RD,
	NULL, 0, sysctl_intrcnt_all, "", "Interrupt Counts");

SYSCTL_PROC(_hw, OID_AUTO, intrcnt, CTLTYPE_OPAQUE | CTLFLAG_RD,
	NULL, 0, sysctl_intrcnt_all, "", "Interrupt Counts");

static void
int_moveto_destcpu(int *orig_cpuid0, int cpuid)
{
    int orig_cpuid = mycpuid;

    if (cpuid != orig_cpuid)
	lwkt_migratecpu(cpuid);

    *orig_cpuid0 = orig_cpuid;
}

static void
int_moveto_origcpu(int orig_cpuid, int cpuid)
{
    if (cpuid != orig_cpuid)
	lwkt_migratecpu(orig_cpuid);
}

static void
intr_init(void *dummy __unused)
{
	int cpuid;

	kprintf("Initialize MI interrupts\n");

	for (cpuid = 0; cpuid < ncpus; ++cpuid) {
		int intr;

		for (intr = 0; intr < MAX_INTS; ++intr) {
			struct intr_info *info = &intr_info_ary[cpuid][intr];

			info->i_cpuid = cpuid;
			info->i_intr = intr;
		}
	}
}
SYSINIT(intr_init, SI_BOOT2_FINISH_PIC, SI_ORDER_ANY, intr_init, NULL);
