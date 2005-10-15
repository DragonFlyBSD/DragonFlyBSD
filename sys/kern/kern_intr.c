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
 * $DragonFly: src/sys/kern/kern_intr.c,v 1.26 2005/10/15 03:23:01 dillon Exp $
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/thread2.h>
#include <sys/random.h>
#include <sys/serialize.h>
#include <sys/bus.h>

#include <machine/ipl.h>
#include <machine/frame.h>

#include <sys/interrupt.h>

typedef struct intrec {
    struct intrec *next;
    inthand2_t	*handler;
    void	*argument;
    char	*name;
    int		intr;
    int		intr_flags;
    struct lwkt_serialize *serializer;
} *intrec_t;

struct intr_info {
	intrec_t	i_reclist;
	struct thread	i_thread;
	struct random_softc i_random;
	int		i_running;
	long		i_count;
	int		i_fast;
	int		i_slow;
	int		i_valid_thread;
} intr_info_ary[NHWI + NSWI];

#define EMERGENCY_INTR_POLLING_FREQ_MAX 20000

static int sysctl_emergency_freq(SYSCTL_HANDLER_ARGS);
static int sysctl_emergency_enable(SYSCTL_HANDLER_ARGS);
static void emergency_intr_timer_callback(systimer_t, struct intrframe *);
static void ithread_handler(void *arg);
static void ithread_emergency(void *arg);

int intr_info_size = sizeof(intr_info_ary) / sizeof(intr_info_ary[0]);

static struct systimer emergency_intr_timer;
static struct thread emergency_intr_thread;

#define LIVELOCK_NONE		0
#define LIVELOCK_LIMITED	1

static int livelock_limit = 50000;
static int livelock_fallback = 20000;
SYSCTL_INT(_kern, OID_AUTO, livelock_limit,
        CTLFLAG_RW, &livelock_limit, 0, "Livelock interrupt rate limit");
SYSCTL_INT(_kern, OID_AUTO, livelock_fallback,
        CTLFLAG_RW, &livelock_fallback, 0, "Livelock interrupt fallback rate");

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
	int error, enabled;

	enabled = emergency_intr_enable;
	error = sysctl_handle_int(oidp, &enabled, 0, req);
	if (error || req->newptr == NULL)
		return error;
	emergency_intr_enable = enabled;
	if (emergency_intr_enable) {
		emergency_intr_timer.periodic = 
			sys_cputimer->fromhz(emergency_intr_freq);
	} else {
		emergency_intr_timer.periodic = sys_cputimer->fromhz(1);
	}
	return 0;
}

static int
sysctl_emergency_freq(SYSCTL_HANDLER_ARGS)
{
        int error, phz;

        phz = emergency_intr_freq;
        error = sysctl_handle_int(oidp, &phz, 0, req);
        if (error || req->newptr == NULL)
                return error;
        if (phz <= 0)
                return EINVAL;
        else if (phz > EMERGENCY_INTR_POLLING_FREQ_MAX)
                phz = EMERGENCY_INTR_POLLING_FREQ_MAX;

        emergency_intr_freq = phz;
	if (emergency_intr_enable) {
		emergency_intr_timer.periodic = 
			sys_cputimer->fromhz(emergency_intr_freq);
	} else {
		emergency_intr_timer.periodic = sys_cputimer->fromhz(1);
	}
        return 0;
}

/*
 * Register an SWI or INTerrupt handler.
 */
void *
register_swi(int intr, inthand2_t *handler, void *arg, const char *name,
		struct lwkt_serialize *serializer)
{
    if (intr < NHWI || intr >= NHWI + NSWI)
	panic("register_swi: bad intr %d", intr);
    return(register_int(intr, handler, arg, name, serializer, 0));
}

void *
register_int(int intr, inthand2_t *handler, void *arg, const char *name,
		struct lwkt_serialize *serializer, int intr_flags)
{
    struct intr_info *info;
    struct intrec **list;
    intrec_t rec;

    if (intr < 0 || intr >= NHWI + NSWI)
	panic("register_int: bad intr %d", intr);
    if (name == NULL)
	name = "???";
    info = &intr_info_ary[intr];

    rec = malloc(sizeof(struct intrec), M_DEVBUF, M_INTWAIT);
    rec->name = malloc(strlen(name) + 1, M_DEVBUF, M_INTWAIT);
    strcpy(rec->name, name);

    rec->handler = handler;
    rec->argument = arg;
    rec->intr = intr;
    rec->intr_flags = intr_flags;
    rec->next = NULL;
    rec->serializer = serializer;

    list = &info->i_reclist;

    /*
     * Keep track of how many fast and slow interrupts we have.
     */
    if (intr_flags & INTR_FAST)
	++info->i_fast;
    else
	++info->i_slow;

    /*
     * Create an emergency polling thread and set up a systimer to wake
     * it up.
     */
    if (emergency_intr_thread.td_kstack == NULL) {
	lwkt_create(ithread_emergency, NULL, NULL,
		    &emergency_intr_thread, TDF_STOPREQ|TDF_INTTHREAD, -1,
		    "ithread emerg");
	systimer_init_periodic_nq(&emergency_intr_timer,
		    emergency_intr_timer_callback, &emergency_intr_thread, 
		    (emergency_intr_enable ? emergency_intr_freq : 1));
    }

    /*
     * Create an interrupt thread if necessary, leave it in an unscheduled
     * state.
     */
    if (info->i_valid_thread == 0) {
	info->i_valid_thread = 1;
	lwkt_create((void *)ithread_handler, (void *)intr, NULL,
	    &info->i_thread, TDF_STOPREQ|TDF_INTTHREAD, -1, 
	    "ithread %d", intr);
	if (intr >= NHWI && intr < NHWI + NSWI)
	    lwkt_setpri(&info->i_thread, TDPRI_SOFT_NORM);
	else
	    lwkt_setpri(&info->i_thread, TDPRI_INT_MED);
	info->i_thread.td_preemptable = lwkt_preempt;
    }

    /*
     * Add the record to the interrupt list
     */
    crit_enter();	/* token */
    while (*list != NULL)
	list = &(*list)->next;
    *list = rec;
    crit_exit();
    return(rec);
}

int
unregister_swi(void *id)
{
    return(unregister_int(id));
}

int
unregister_int(void *id)
{
    struct intr_info *info;
    struct intrec **list;
    intrec_t rec;
    int intr;

    intr = ((intrec_t)id)->intr;

    if (intr < 0 || intr > NHWI + NSWI)
	panic("register_int: bad intr %d", intr);

    info = &intr_info_ary[intr];

    /*
     * Remove the interrupt descriptor
     */
    crit_enter();
    list = &info->i_reclist;
    while ((rec = *list) != NULL) {
	if (rec == id) {
	    *list = rec->next;
	    break;
	}
	list = &rec->next;
    }
    crit_exit();

    /*
     * Free it, adjust interrupt type counts
     */
    if (rec != NULL) {
	if (rec->intr_flags & INTR_FAST)
	    --info->i_fast;
	else
	    --info->i_slow;
	free(rec->name, M_DEVBUF);
	free(rec, M_DEVBUF);
    } else {
	printf("warning: unregister_int: int %d handler for %s not found\n",
		intr, ((intrec_t)id)->name);
    }

    /*
     * Return the number of interrupt vectors still registered on this intr
     */
    return(info->i_fast + info->i_slow);
}

int
get_registered_intr(void *id)
{
    return(((intrec_t)id)->intr);
}

const char *
get_registered_name(int intr)
{
    intrec_t rec;

    if (intr < 0 || intr > NHWI + NSWI)
	panic("register_int: bad intr %d", intr);

    if ((rec = intr_info_ary[intr].i_reclist) == NULL)
	return(NULL);
    else if (rec->next)
	return("mux");
    else
	return(rec->name);
}

int
count_registered_ints(int intr)
{
    struct intr_info *info;

    if (intr < 0 || intr > NHWI + NSWI)
	panic("register_int: bad intr %d", intr);
    info = &intr_info_ary[intr];
    return(info->i_fast + info->i_slow);
}

long
get_interrupt_counter(int intr)
{
    struct intr_info *info;

    if (intr < 0 || intr > NHWI + NSWI)
	panic("register_int: bad intr %d", intr);
    info = &intr_info_ary[intr];
    return(info->i_count);
}


void
swi_setpriority(int intr, int pri)
{
    struct intr_info *info;

    if (intr < NHWI || intr >= NHWI + NSWI)
	panic("register_swi: bad intr %d", intr);
    info = &intr_info_ary[intr];
    if (info->i_valid_thread)
	lwkt_setpri(&info->i_thread, pri);
}

void
register_randintr(int intr)
{
    struct intr_info *info;

    if ((unsigned int)intr >= NHWI + NSWI)
	panic("register_randintr: bad intr %d", intr);
    info = &intr_info_ary[intr];
    info->i_random.sc_intr = intr;
    info->i_random.sc_enabled = 1;
}

void
unregister_randintr(int intr)
{
    struct intr_info *info;

    if (intr < NHWI || intr >= NHWI + NSWI)
	panic("register_swi: bad intr %d", intr);
    info = &intr_info_ary[intr];
    info->i_random.sc_enabled = 0;
}

/*
 * Dispatch an interrupt.  If there's nothing to do we have a stray
 * interrupt and can just return, leaving the interrupt masked.
 *
 * We need to schedule the interrupt and set its i_running bit.  If
 * we are not on the interrupt thread's cpu we have to send a message
 * to the correct cpu that will issue the desired action (interlocking
 * with the interrupt thread's critical section).
 *
 * We are NOT in a critical section, which will allow the scheduled
 * interrupt to preempt us.  The MP lock might *NOT* be held here.
 */
static void
sched_ithd_remote(void *arg)
{
    sched_ithd((int)arg);
}

void
sched_ithd(int intr)
{
    struct intr_info *info;

    info = &intr_info_ary[intr];

    ++info->i_count;
    if (info->i_valid_thread) {
	if (info->i_reclist == NULL) {
	    printf("sched_ithd: stray interrupt %d\n", intr);
	} else {
	    if (info->i_thread.td_gd == mycpu) {
		info->i_running = 1;
		/* preemption handled internally */
		lwkt_schedule(&info->i_thread);
	    } else {
		lwkt_send_ipiq(info->i_thread.td_gd, 
				sched_ithd_remote, (void *)intr);
	    }
	}
    } else {
	printf("sched_ithd: stray interrupt %d\n", intr);
    }
}

/*
 * This is run from a periodic SYSTIMER (and thus must be MP safe, the BGL
 * might not be held).
 */
static void
ithread_livelock_wakeup(systimer_t st)
{
    struct intr_info *info;

    info = &intr_info_ary[(int)st->data];
    if (info->i_valid_thread)
	lwkt_schedule(&info->i_thread);
}

/*
 * This function is called drectly from the ICU or APIC vector code assembly
 * to process an interrupt.  The critical section and interrupt deferral
 * checks have already been done but the function is entered WITHOUT
 * a critical section held.  The BGL may or may not be held.
 *
 * Must return non-zero if we do not want the vector code to re-enable
 * the interrupt (which we don't if we have to schedule the interrupt)
 */
int ithread_fast_handler(struct intrframe frame);

int
ithread_fast_handler(struct intrframe frame)
{
    int intr;
    struct intr_info *info;
    struct intrec **list;
    int must_schedule;
#ifdef SMP
    int got_mplock;
#endif
    intrec_t rec, next_rec;
    globaldata_t gd;

    intr = frame.if_vec;
    gd = mycpu;

    info = &intr_info_ary[intr];

    /*
     * If we are not processing any FAST interrupts, just schedule the thing.
     * (since we aren't in a critical section, this can result in a
     * preemption)
     */
    if (info->i_fast == 0) {
	sched_ithd(intr);
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
    crit_enter_gd(gd);
    ++gd->gd_intr_nesting_level;
    ++gd->gd_cnt.v_intr;
    must_schedule = info->i_slow;
#ifdef SMP
    got_mplock = 0;
#endif

    list = &info->i_reclist;
    for (rec = *list; rec; rec = next_rec) {
	next_rec = rec->next;	/* rec may be invalid after call */

	if (rec->intr_flags & INTR_FAST) {
#ifdef SMP
	    if ((rec->intr_flags & INTR_MPSAFE) == 0 && got_mplock == 0) {
		if (try_mplock() == 0) {
		    /*
		     * XXX forward to the cpu holding the MP lock
		     */
		    must_schedule = 1;
		    break;
		}
		got_mplock = 1;
	    }
#endif
	    if (rec->serializer) {
		must_schedule += lwkt_serialize_handler_try(
					rec->serializer, rec->handler,
					rec->argument, &frame);
	    } else {
		rec->handler(rec->argument, &frame);
	    }
	}
    }

    /*
     * Cleanup
     */
    --gd->gd_intr_nesting_level;
#ifdef SMP
    if (got_mplock)
	rel_mplock();
#endif
    crit_exit_gd(gd);

    /*
     * If we had a problem, schedule the thread to catch the missed
     * records (it will just re-run all of them).  A return value of 0
     * indicates that all handlers have been run and the interrupt can
     * be re-enabled, and a non-zero return indicates that the interrupt
     * thread controls re-enablement.
     */
    if (must_schedule)
	sched_ithd(intr);
    else
	++info->i_count;
    return(must_schedule);
}

#if 0

6: ;                                                                    \
        /* could not get the MP lock, forward the interrupt */          \
        movl    mp_lock, %eax ;          /* check race */               \
        cmpl    $MP_FREE_LOCK,%eax ;                                    \
        je      2b ;                                                    \
        incl    PCPU(cnt)+V_FORWARDED_INTS ;                            \
        subl    $12,%esp ;                                              \
        movl    $irq_num,8(%esp) ;                                      \
        movl    $forward_fastint_remote,4(%esp) ;                       \
        movl    %eax,(%esp) ;                                           \
        call    lwkt_send_ipiq_bycpu ;                                  \
        addl    $12,%esp ;                                              \
        jmp     5f ;                   

#endif


/*
 * Interrupt threads run this as their main loop.
 *
 * The handler begins execution outside a critical section and with the BGL
 * held.
 *
 * The i_running state starts at 0.  When an interrupt occurs, the hardware
 * interrupt is disabled and sched_ithd() The HW interrupt remains disabled
 * until all routines have run.  We then call ithread_done() to reenable 
 * the HW interrupt and deschedule us until the next interrupt. 
 *
 * We are responsible for atomically checking i_running and ithread_done()
 * is responsible for atomically checking for platform-specific delayed
 * interrupts.  i_running for our irq is only set in the context of our cpu,
 * so a critical section is a sufficient interlock.
 */
#define LIVELOCK_TIMEFRAME(freq)	((freq) >> 2)	/* 1/4 second */

static void
ithread_handler(void *arg)
{
    struct intr_info *info;
    u_int cputicks;
    u_int bticks;
    int intr;
    int freq;
    struct intrec **list;
    intrec_t rec, nrec;
    globaldata_t gd = mycpu;
    struct systimer ill_timer;	/* enforced freq. timer */
    struct systimer ill_rtimer;	/* recovery timer */
    u_int ill_count = 0;	/* interrupt livelock counter */
    u_int ill_ticks = 0;	/* track elapsed to calculate freq */
    u_int ill_delta = 0;	/* track elapsed to calculate freq */
    int ill_state = 0;		/* current state */

    intr = (int)arg;
    info = &intr_info_ary[intr];
    list = &info->i_reclist;
    gd = mycpu;

    /*
     * The loop must be entered with one critical section held.
     */
    crit_enter_gd(gd);

    for (;;) {
	/*
	 * We can get woken up by the livelock periodic code too, run the 
	 * handlers only if there is a real interrupt pending.  XXX
	 *
	 * Clear i_running prior to running the handlers to interlock
	 * again new events occuring during processing of existing events.
	 *
	 * Run each handler in a critical section.  Note that we run both
	 * FAST and SLOW designated service routines.
	 */
	info->i_running = 0;
	for (rec = *list; rec; rec = nrec) {
	    nrec = rec->next;
	    if (rec->serializer) {
		lwkt_serialize_handler_call(rec->serializer,
					rec->handler, rec->argument, NULL);
	    } else {
		rec->handler(rec->argument, NULL);
	    }
	}

	/*
	 * Do a quick exit/enter to catch any higher-priority
	 * interrupt sources and so user/system/interrupt statistics
	 * work for interrupt threads.
	 */
	crit_exit_gd(gd);
	crit_enter_gd(gd);

	/*
	 * This is our interrupt hook to add rate randomness to the random
	 * number generator.
	 */
	if (info->i_random.sc_enabled)
	    add_interrupt_randomness(intr);

	/*
	 * This is our livelock test.  If we hit the rate limit we
	 * limit ourselves to X interrupts/sec until the rate
	 * falls below 50% of that value, then we unlimit again.
	 *
	 * XXX calling cputimer_count() is expensive but a livelock may
	 * prevent other interrupts from occuring so we cannot use ticks.
	 */
	cputicks = sys_cputimer->count();
	++ill_count;
	bticks = cputicks - ill_ticks;
	ill_ticks = cputicks;
	if (bticks > sys_cputimer->freq)
	    bticks = sys_cputimer->freq;

	switch(ill_state) {
	case LIVELOCK_NONE:
	    ill_delta += bticks;
	    if (ill_delta < LIVELOCK_TIMEFRAME(sys_cputimer->freq))
		break;
	    freq = (int64_t)ill_count * sys_cputimer->freq / 
		   ill_delta;
	    ill_delta = 0;
	    ill_count = 0;
	    if (freq < livelock_limit)
		break;
	    printf("intr %d at %d hz, livelocked! limiting at %d hz\n",
		intr, freq, livelock_fallback);
	    ill_state = LIVELOCK_LIMITED;
	    bticks = 0;
	    /* force periodic check to avoid stale removal (if ints stop) */
	    systimer_init_periodic(&ill_rtimer, ithread_livelock_wakeup,
				    (void *)intr, 1);
	    /* fall through */
	case LIVELOCK_LIMITED:
	    /*
	     * Delay (us) before rearming the interrupt
	     */
	    systimer_init_oneshot(&ill_timer, ithread_livelock_wakeup,
				(void *)intr, 1 + 1000000 / livelock_fallback);
	    lwkt_deschedule_self(curthread);
	    lwkt_switch();

	    /* in case we were woken up by something else */
	    systimer_del(&ill_timer);

	    /*
	     * Calculate interrupt rate (note that due to our delay it
	     * will not exceed livelock_fallback).
	     */
	    ill_delta += bticks;
	    if (ill_delta < LIVELOCK_TIMEFRAME(sys_cputimer->freq))
		break;
	    freq = (int64_t)ill_count * sys_cputimer->freq / ill_delta;
	    ill_delta = 0;
	    ill_count = 0;
	    if (freq < (livelock_fallback >> 1)) {
		printf("intr %d at %d hz, removing livelock limit\n",
			intr, freq);
		ill_state = LIVELOCK_NONE;
		systimer_del(&ill_rtimer);
	    }
	    break;
	}

	/*
	 * There are two races here.  i_running is set by sched_ithd()
	 * in the context of our cpu and is critical-section safe.  We
	 * are responsible for checking it.  ipending is not critical
	 * section safe and must be handled by the platform specific
	 * ithread_done() routine.
	 */
	if (info->i_running == 0)
	    ithread_done(intr);
	/* must be in critical section on loop */
    }
    /* not reached */
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
    struct intr_info *info;
    intrec_t rec, nrec;
    int intr;

    for (;;) {
	for (intr = 0; intr < NHWI + NSWI; ++intr) {
	    info = &intr_info_ary[intr];
	    for (rec = info->i_reclist; rec; rec = nrec) {
		if ((rec->intr_flags & INTR_NOPOLL) == 0) {
		    if (rec->serializer) {
			lwkt_serialize_handler_call(rec->serializer,
						rec->handler, rec->argument, NULL);
		    } else {
			rec->handler(rec->argument, NULL);
		    }
		}
		nrec = rec->next;
	    }
	}
	lwkt_deschedule_self(curthread);
	lwkt_switch();
    }
}

/*
 * Systimer callback - schedule the emergency interrupt poll thread
 * 		       if emergency polling is enabled.
 */
static
void
emergency_intr_timer_callback(systimer_t info, struct intrframe *frame __unused)
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
    int intr;
    char buf[64];

    for (intr = 0; error == 0 && intr < NHWI + NSWI; ++intr) {
	info = &intr_info_ary[intr];

	len = 0;
	buf[0] = 0;
	for (rec = info->i_reclist; rec; rec = rec->next) {
	    snprintf(buf + len, sizeof(buf) - len, "%s%s", 
		(len ? "/" : ""), rec->name);
	    len += strlen(buf + len);
	}
	if (len == 0) {
	    snprintf(buf, sizeof(buf), "irq%d", intr);
	    len = strlen(buf);
	}
	error = SYSCTL_OUT(req, buf, len + 1);
    }
    return (error);
}


SYSCTL_PROC(_hw, OID_AUTO, intrnames, CTLTYPE_OPAQUE | CTLFLAG_RD,
	NULL, 0, sysctl_intrnames, "", "Interrupt Names");

static int
sysctl_intrcnt(SYSCTL_HANDLER_ARGS)
{
    struct intr_info *info;
    int error = 0;
    int intr;

    for (intr = 0; intr < NHWI + NSWI; ++intr) {
	info = &intr_info_ary[intr];

	error = SYSCTL_OUT(req, &info->i_count, sizeof(info->i_count));
	if (error)
		break;
    }
    return(error);
}

SYSCTL_PROC(_hw, OID_AUTO, intrcnt, CTLTYPE_OPAQUE | CTLFLAG_RD,
	NULL, 0, sysctl_intrcnt, "", "Interrupt Counts");

