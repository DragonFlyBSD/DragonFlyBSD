/*
 * Copyright (c) 2003, Matthew Dillon <dillon@backplane.com> All rights reserved.
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
 * $DragonFly: src/sys/kern/kern_intr.c,v 1.16 2004/06/28 02:33:04 dillon Exp $
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

#include <machine/ipl.h>

#include <sys/interrupt.h>

typedef struct intrec {
    struct intrec *next;
    inthand2_t	*handler;
    void	*argument;
    const char	*name;
    int		intr;
} intrec_t;

static intrec_t	*intlists[NHWI+NSWI];
static thread_t ithreads[NHWI+NSWI];
static struct thread ithread_ary[NHWI+NSWI];
static struct random_softc irandom_ary[NHWI+NSWI];
static int irunning[NHWI+NSWI];
static u_int ill_count[NHWI+NSWI];	/* interrupt livelock counter */
static u_int ill_ticks[NHWI+NSWI];	/* track elapsed to calculate freq */
static u_int ill_delta[NHWI+NSWI];	/* track elapsed to calculate freq */
static int ill_state[NHWI+NSWI];	/* current state */
static struct systimer ill_timer[NHWI+NSWI];	/* enforced freq. timer */
static struct systimer ill_rtimer[NHWI+NSWI];	/* recovery timer */

#define LIVELOCK_NONE		0
#define LIVELOCK_LIMITED	1

static int livelock_limit = 50000;
static int livelock_fallback = 20000;
SYSCTL_INT(_kern, OID_AUTO, livelock_limit,
        CTLFLAG_RW, &livelock_limit, 0, "Livelock interrupt rate limit");
SYSCTL_INT(_kern, OID_AUTO, livelock_fallback,
        CTLFLAG_RW, &livelock_fallback, 0, "Livelock interrupt fallback rate");

static void ithread_handler(void *arg);

thread_t
register_swi(int intr, inthand2_t *handler, void *arg, const char *name)
{
    if (intr < NHWI || intr >= NHWI + NSWI)
	panic("register_swi: bad intr %d", intr);
    return(register_int(intr, handler, arg, name));
}

thread_t
register_int(int intr, inthand2_t *handler, void *arg, const char *name)
{
    intrec_t **list;
    intrec_t *rec;
    thread_t td;

    if (intr < 0 || intr >= NHWI + NSWI)
	panic("register_int: bad intr %d", intr);

    rec = malloc(sizeof(intrec_t), M_DEVBUF, M_NOWAIT);
    if (rec == NULL)
	panic("register_swi: malloc failed");
    rec->handler = handler;
    rec->argument = arg;
    rec->name = name;
    rec->intr = intr;
    rec->next = NULL;

    list = &intlists[intr];

    /*
     * Create an interrupt thread if necessary, leave it in an unscheduled
     * state.  The kthread restore function exits a critical section before
     * starting the function so we need *TWO* critical sections in order
     * for the handler to begin running in one.
     */
    if ((td = ithreads[intr]) == NULL) {
	lwkt_create((void *)ithread_handler, (void *)intr, &ithreads[intr],
	    &ithread_ary[intr], TDF_STOPREQ|TDF_INTTHREAD, -1, 
	    "ithread %d", intr);
	td = ithreads[intr];
	if (intr >= NHWI && intr < NHWI + NSWI)
	    lwkt_setpri(td, TDPRI_SOFT_NORM + TDPRI_CRIT * 2);
	else
	    lwkt_setpri(td, TDPRI_INT_MED + TDPRI_CRIT * 2);
    }

    /*
     * Add the record to the interrupt list
     */
    crit_enter();	/* token */
    while (*list != NULL)
	list = &(*list)->next;
    *list = rec;
    crit_exit();
    return(td);
}

void
unregister_swi(int intr, inthand2_t *handler)
{
    if (intr < NHWI || intr >= NHWI + NSWI)
	panic("register_swi: bad intr %d", intr);
    unregister_int(intr, handler);
}

void
unregister_int(int intr, inthand2_t handler)
{
    intrec_t **list;
    intrec_t *rec;

    if (intr < 0 || intr > NHWI + NSWI)
	panic("register_int: bad intr %d", intr);
    list = &intlists[intr];
    crit_enter();
    while ((rec = *list) != NULL) {
	if (rec->handler == (void *)handler) {
	    *list = rec->next;
	    break;
	}
	list = &rec->next;
    }
    crit_exit();
    if (rec != NULL) {
	free(rec, M_DEVBUF);
    } else {
	printf("warning: unregister_int: int %d handler %p not found\n",
	    intr, handler);
    }
}

void
swi_setpriority(int intr, int pri)
{
    struct thread *td;

    if (intr < NHWI || intr >= NHWI + NSWI)
	panic("register_swi: bad intr %d", intr);
    if ((td = ithreads[intr]) != NULL)
	lwkt_setpri(td, pri);
}

void
register_randintr(int intr)
{
    struct random_softc *sc = &irandom_ary[intr];
    sc->sc_intr = intr;
    sc->sc_enabled = 1;
}

void
unregister_randintr(int intr)
{
    struct random_softc *sc = &irandom_ary[intr];
    sc->sc_enabled = 0;
}

/*
 * Dispatch an interrupt.  If there's nothing to do we have a stray
 * interrupt and can just return, leaving the interrupt masked.
 *
 * We need to schedule the interrupt and set its irunning[] bit.  If
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
    thread_t td;

    if ((td = ithreads[intr]) != NULL) {
	if (intlists[intr] == NULL) {
	    printf("sched_ithd: stray interrupt %d\n", intr);
	} else {
	    if (td->td_gd == mycpu) {
		irunning[intr] = 1;
		lwkt_schedule(td);	/* preemption handled internally */
	    } else {
		lwkt_send_ipiq(td->td_gd, sched_ithd_remote, (void *)intr);
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
ithread_livelock_wakeup(systimer_t info)
{
    int intr = (int)info->data;
    thread_t td;

    if ((td = ithreads[intr]) != NULL)
	lwkt_schedule(td);
}


/*
 * Interrupt threads run this as their main loop.  The handler should be
 * in a critical section on entry and the BGL is usually left held (for now).
 *
 * The irunning state starts at 0.  When an interrupt occurs, the hardware
 * interrupt is disabled and sched_ithd() The HW interrupt remains disabled
 * until all routines have run.  We then call ithread_done() to reenable 
 * the HW interrupt and deschedule us until the next interrupt.
 */

#define LIVELOCK_TIMEFRAME(freq)	((freq) >> 2)	/* 1/4 second */

static void
ithread_handler(void *arg)
{
    int intr = (int)arg;
    int freq;
    u_int bticks;
    u_int cputicks;
    intrec_t **list = &intlists[intr];
    intrec_t *rec;
    intrec_t *nrec;
    struct random_softc *sc = &irandom_ary[intr];

    KKASSERT(curthread->td_pri >= TDPRI_CRIT);
    for (;;) {
	/*
	 * We can get woken up by the livelock periodic code too, run the 
	 * handlers only if there is a real interrupt pending.  Clear
	 * irunning[] prior to running the handlers to interlock new
	 * events.
	 */
	if (irunning[intr]) {
	    irunning[intr] = 0;
	    for (rec = *list; rec; rec = nrec) {
		nrec = rec->next;
		rec->handler(rec->argument);
	    }
	}

	/*
	 * This is our interrupt hook to add rate randomness to the random
	 * number generator.
	 */
	if (sc->sc_enabled)
	    add_interrupt_randomness(intr);

	/*
	 * This is our livelock test.  If we hit the rate limit we
	 * limit ourselves to 10000 interrupts/sec until the rate
	 * falls below 50% of that value, then we unlimit again.
	 */
	cputicks = cputimer_count();
	++ill_count[intr];
	bticks = cputicks - ill_ticks[intr];
	ill_ticks[intr] = cputicks;
	if (bticks > cputimer_freq)
	    bticks = cputimer_freq;

	switch(ill_state[intr]) {
	case LIVELOCK_NONE:
	    ill_delta[intr] += bticks;
	    if (ill_delta[intr] < LIVELOCK_TIMEFRAME(cputimer_freq))
		break;
	    freq = (int64_t)ill_count[intr] * cputimer_freq / ill_delta[intr];
	    ill_delta[intr] = 0;
	    ill_count[intr] = 0;
	    if (freq < livelock_limit)
		break;
	    printf("intr %d at %d hz, livelocked! limiting at %d hz\n",
		intr, freq, livelock_fallback);
	    ill_state[intr] = LIVELOCK_LIMITED;
	    bticks = 0;
	    /* force periodic check to avoid stale removal (if ints stop) */
	    systimer_init_periodic(&ill_rtimer[intr], ithread_livelock_wakeup,
				(void *)intr, 1);
	    /* fall through */
	case LIVELOCK_LIMITED:
	    /*
	     * Delay (us) before rearming the interrupt
	     */
	    systimer_init_oneshot(&ill_timer[intr], ithread_livelock_wakeup,
				(void *)intr, 1 + 1000000 / livelock_fallback);
	    lwkt_deschedule_self(curthread);
	    lwkt_switch();

	    /* in case we were woken up by something else */
	    systimer_del(&ill_timer[intr]);

	    /*
	     * Calculate interrupt rate (note that due to our delay it
	     * will not exceed livelock_fallback).
	     */
	    ill_delta[intr] += bticks;
	    if (ill_delta[intr] < LIVELOCK_TIMEFRAME(cputimer_freq))
		break;
	    freq = (int64_t)ill_count[intr] * cputimer_freq / ill_delta[intr];
	    ill_delta[intr] = 0;
	    ill_count[intr] = 0;
	    if (freq < (livelock_fallback >> 1)) {
		printf("intr %d at %d hz, removing livelock limit\n",
			intr, freq);
		ill_state[intr] = LIVELOCK_NONE;
		systimer_del(&ill_rtimer[intr]);
	    }
	    break;
	}

	/*
	 * If another interrupt has not been queued we can reenable the
	 * hardware interrupt and go to sleep.
	 */
	if (irunning[intr] == 0)
	    ithread_done(intr);
    }
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
	return (sysctl_handle_opaque(oidp, intrnames, eintrnames - intrnames, 
	    req));
}

SYSCTL_PROC(_hw, OID_AUTO, intrnames, CTLTYPE_OPAQUE | CTLFLAG_RD,
	NULL, 0, sysctl_intrnames, "", "Interrupt Names");

static int
sysctl_intrcnt(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_handle_opaque(oidp, intrcnt, 
	    (char *)eintrcnt - (char *)intrcnt, req));
}

SYSCTL_PROC(_hw, OID_AUTO, intrcnt, CTLTYPE_OPAQUE | CTLFLAG_RD,
	NULL, 0, sysctl_intrcnt, "", "Interrupt Counts");
