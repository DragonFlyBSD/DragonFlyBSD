/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/kern/kern_cputimer.c,v 1.5 2008/06/05 18:06:32 swildner Exp $
 */
/*
 * Generic cputimer - access to a reliable, free-running counter.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/systimer.h>
#include <sys/sysctl.h>
#include <sys/thread2.h>

static sysclock_t dummy_cputimer_count(void);

static struct cputimer dummy_cputimer = {
    SLIST_ENTRY_INITIALIZER,
    "dummy",
    CPUTIMER_PRI_DUMMY,
    CPUTIMER_DUMMY,
    dummy_cputimer_count,
    cputimer_default_fromhz,
    cputimer_default_fromus,
    cputimer_default_construct,
    cputimer_default_destruct,
    1000000,
    (1000000LL << 32) / 1000000,
    (1000000000LL << 32) / 1000000,
    0
};

struct cputimer *sys_cputimer = &dummy_cputimer;
SLIST_HEAD(, cputimer) cputimerhead = SLIST_HEAD_INITIALIZER(&cputimerhead);

/*
 * Generic cputimer API
 */
void
cputimer_select(struct cputimer *timer, int pri)
{
    sysclock_t oldclock;

    /*
     * Calculate helper fields
     */
    cputimer_set_frequency(timer, timer->freq);

    /*
     * Install a new cputimer if its priority allows it.  If timer is
     * passed as NULL we deinstall the current timer and revert to our
     * dummy.
     */
    if (pri == 0)
	pri = timer->pri;
    if (timer == NULL || pri >= sys_cputimer->pri) {
	oldclock = sys_cputimer->count();
	sys_cputimer->destruct(sys_cputimer);
	sys_cputimer = &dummy_cputimer;
	if (timer) {
	    sys_cputimer = timer;
	    timer->construct(timer, oldclock);
	    cputimer_intr_config(timer);
	}
    }
}

/*
 * Register a timer.  If the timer has already been registered, do nothing.
 */
void
cputimer_register(struct cputimer *timer)
{
    struct cputimer *scan;

    /*
     * Initialize dummy_cputimer if the slist is empty, it does not get
     * registered the normal way.
     */
    if (SLIST_EMPTY(&cputimerhead))
	SLIST_FIRST(&cputimerhead) = &dummy_cputimer;
    SLIST_FOREACH(scan, &cputimerhead, next) {
	if (scan == timer)
	    return;
    }
    SLIST_INSERT_HEAD(&cputimerhead, timer, next);
}

/*
 * Deregister a timer.  If the timer has already been deregistered, do nothing.
 */
void
cputimer_deregister(struct cputimer *timer)
{
    struct cputimer *scan;
    struct cputimer *best;

    /*
     * Locate and remove the timer.  If the timer is our currently active
     * timer, revert to the dummy timer.
     */
    SLIST_FOREACH(scan, &cputimerhead, next) {
	    if (timer == scan) {
		if (timer == sys_cputimer)
		    cputimer_select(&dummy_cputimer, 0x7FFFFFFF);
		SLIST_REMOVE(&cputimerhead, timer, cputimer, next);
		break;
	    }
    }

    /*
     * If sys_cputimer reverted to the dummy, select the best one
     */
    if (sys_cputimer == &dummy_cputimer) {
	best = NULL;
	SLIST_FOREACH(scan, &cputimerhead, next) {
	    if (best == NULL || scan->pri > best->pri)
		best = scan;
	}
	if (best)
	    cputimer_select(best, 0x7FFFFFFF);
    }
}

/*
 * Calculate usec / tick and nsec / tick, scaled by (1 << 32).
 *
 * so e.g. a 3 mhz timer would be 3 usec / tick x (1 << 32),
 * or 3000 nsec / tick x (1 << 32)
 */
void
cputimer_set_frequency(struct cputimer *timer, sysclock_t freq)
{
    timer->freq = freq;
    timer->freq64_usec = (1000000LL << 32) / freq;
    timer->freq64_nsec = (1000000000LL << 32) / freq;
    if (timer == sys_cputimer)
	cputimer_intr_config(timer);
}

sysclock_t
cputimer_default_fromhz(int freq)
{
    return(sys_cputimer->freq / freq + 1);
}

sysclock_t
cputimer_default_fromus(int us)
{
    return((int64_t)sys_cputimer->freq * us / 1000000);
}

/*
 * Dummy counter implementation
 */
static
sysclock_t
dummy_cputimer_count(void)
{
    return(++dummy_cputimer.base);
}

void
cputimer_default_construct(struct cputimer *cputimer, sysclock_t oldclock)
{
    cputimer->base = oldclock;
}

void
cputimer_default_destruct(struct cputimer *cputimer)
{
}

/************************************************************************
 *				SYSCTL SUPPORT				*
 ************************************************************************
 *
 * Note: the ability to change the systimer is not currently enabled
 * because it will mess up systimer calculations.  You have to live
 * with what is configured at boot.
 */
static int
sysctl_cputimer_reglist(SYSCTL_HANDLER_ARGS)
{
    struct cputimer *scan;
    int error = 0;
    int loop = 0;

    /*
     * Build a list of available timers
     */
    SLIST_FOREACH(scan, &cputimerhead, next) {
	if (error == 0 && loop)
	    error = SYSCTL_OUT(req, " ", 1);
	if (error == 0)
	    error = SYSCTL_OUT(req, scan->name, strlen(scan->name));
	++loop;
    }
    return (error);
}

static int
sysctl_cputimer_name(SYSCTL_HANDLER_ARGS)
{
    int error;

    error = SYSCTL_OUT(req, sys_cputimer->name, strlen(sys_cputimer->name));
    return (error);
}

static int
sysctl_cputimer_clock(SYSCTL_HANDLER_ARGS)
{
    sysclock_t clock;
    int error;

    clock = sys_cputimer->count();
    error = SYSCTL_OUT(req, &clock, sizeof(clock));
    return (error);
}

static int
sysctl_cputimer_freq(SYSCTL_HANDLER_ARGS)
{
    int error;

    error = SYSCTL_OUT(req, &sys_cputimer->freq, sizeof(sys_cputimer->freq));
    return (error);
}

SYSCTL_DECL(_kern_cputimer);
SYSCTL_NODE(_kern, OID_AUTO, cputimer, CTLFLAG_RW, NULL, "cputimer");

SYSCTL_PROC(_kern_cputimer, OID_AUTO, select, CTLTYPE_STRING|CTLFLAG_RD,
	    NULL, 0, sysctl_cputimer_reglist, "A", "");
SYSCTL_PROC(_kern_cputimer, OID_AUTO, name, CTLTYPE_STRING|CTLFLAG_RD,
	    NULL, 0, sysctl_cputimer_name, "A", "");
SYSCTL_PROC(_kern_cputimer, OID_AUTO, clock, CTLTYPE_UINT|CTLFLAG_RD,
	    NULL, 0, sysctl_cputimer_clock, "IU", "");
SYSCTL_PROC(_kern_cputimer, OID_AUTO, freq, CTLTYPE_INT|CTLFLAG_RD,
	    NULL, 0, sysctl_cputimer_freq, "I", "");

static struct cputimer_intr *sys_cputimer_intr;
static uint32_t cputimer_intr_caps;
SLIST_HEAD(, cputimer_intr) cputimer_intr_head =
	SLIST_HEAD_INITIALIZER(&cputimer_intr_head);

void
cputimer_intr_register(struct cputimer_intr *cti)
{
    struct cputimer_intr *scan;

    SLIST_FOREACH(scan, &cputimer_intr_head, next) {
	if (scan == cti)
	    return;
    }
    cti->config(cti, sys_cputimer);
    SLIST_INSERT_HEAD(&cputimer_intr_head, cti, next);
}

void
cputimer_intr_deregister(struct cputimer_intr *cti)
{
    KKASSERT(cti != sys_cputimer_intr);
    SLIST_REMOVE(&cputimer_intr_head, cti, cputimer_intr, next);
}

int
cputimer_intr_select(struct cputimer_intr *cti, int prio)
{
    KKASSERT(cti != NULL);

    if (prio == 0)
	prio = cti->prio;

    if (sys_cputimer_intr == NULL) {
	KKASSERT(cputimer_intr_caps == 0);
	sys_cputimer_intr = cti;
	return 0;
    }

    if ((cti->caps & cputimer_intr_caps) == cputimer_intr_caps) {
	if (prio > sys_cputimer_intr->prio) {
	    sys_cputimer_intr = cti;
	    return 0;
	} else {
	    return EBUSY;
	}
    } else {
	return EOPNOTSUPP;
    }
}

void
cputimer_intr_default_enable(struct cputimer_intr *cti __unused)
{
}

void
cputimer_intr_default_restart(struct cputimer_intr *cti)
{
    cti->reload(cti, 0);
}

void
cputimer_intr_default_config(struct cputimer_intr *cti __unused,
			     const struct cputimer *timer __unused)
{
}

void
cputimer_intr_default_pmfixup(struct cputimer_intr *cti __unused)
{
}

void
cputimer_intr_default_initclock(struct cputimer_intr *cti __unused,
				boolean_t selected __unused)
{
}

void
cputimer_intr_enable(void)
{
    struct cputimer_intr *cti;

    SLIST_FOREACH(cti, &cputimer_intr_head, next)
	cti->enable(cti);
}

void
cputimer_intr_config(const struct cputimer *timer)
{
    struct cputimer_intr *cti;

    SLIST_FOREACH(cti, &cputimer_intr_head, next)
	cti->config(cti, timer);
}

void
cputimer_intr_pmfixup(void)
{
    struct cputimer_intr *cti;

    SLIST_FOREACH(cti, &cputimer_intr_head, next)
	cti->pmfixup(cti);
}

void
cputimer_intr_reload(sysclock_t reload)
{
    struct cputimer_intr *cti = sys_cputimer_intr;

    cti->reload(cti, reload);
}

void
cputimer_intr_restart(void)
{
    struct cputimer_intr *cti = sys_cputimer_intr;

    cti->restart(cti);
}

int
cputimer_intr_select_caps(uint32_t caps)
{
    struct cputimer_intr *cti, *maybe;
    int error;

    maybe = NULL;
    SLIST_FOREACH(cti, &cputimer_intr_head, next) {
	if ((cti->caps & caps) == caps) {
	    if (maybe == NULL)
		maybe = cti;
	    else if (cti->prio > maybe->prio)
		maybe = cti;
	}
    }
    if (maybe == NULL)
	return ENOENT;

    if (sys_cputimer_intr == maybe)
    	return 0;

    cputimer_intr_caps = caps;
    error = cputimer_intr_select(maybe, CPUTIMER_INTR_PRIO_MAX);
    KKASSERT(!error);

    return ERESTART;
}

static void
cputimer_intr_initclocks(void)
{
    struct cputimer_intr *cti, *ncti;

    /*
     * An interrupt cputimer may deregister itself,
     * so use SLIST_FOREACH_MUTABLE here.
     */
    SLIST_FOREACH_MUTABLE(cti, &cputimer_intr_head, next, ncti) {
	boolean_t selected = FALSE;

	if (cti == sys_cputimer_intr)
	    selected = TRUE;
	cti->initclock(cti, selected);
    }
}
/* NOTE: Must be SECOND to allow platform initialization to go first */
SYSINIT(cputimer_intr, SI_BOOT2_CLOCKREG, SI_ORDER_SECOND,
	cputimer_intr_initclocks, NULL)

static int
sysctl_cputimer_intr_reglist(SYSCTL_HANDLER_ARGS)
{
    struct cputimer_intr *scan;
    int error = 0;
    int loop = 0;

    /*
     * Build a list of available interrupt cputimers
     */
    SLIST_FOREACH(scan, &cputimer_intr_head, next) {
	if (error == 0 && loop)
	    error = SYSCTL_OUT(req, " ", 1);
	if (error == 0)
	    error = SYSCTL_OUT(req, scan->name, strlen(scan->name));
	++loop;
    }
    return (error);
}

static int
sysctl_cputimer_intr_freq(SYSCTL_HANDLER_ARGS)
{
    int error;

    error = SYSCTL_OUT(req, &sys_cputimer_intr->freq,
    		       sizeof(sys_cputimer_intr->freq));
    return (error);
}

static int
sysctl_cputimer_intr_select(SYSCTL_HANDLER_ARGS)
{
    struct cputimer_intr *cti;
    char name[32];
    int error;

    ksnprintf(name, sizeof(name), "%s", sys_cputimer_intr->name);
    error = sysctl_handle_string(oidp, name, sizeof(name), req);
    if (error != 0 || req->newptr == NULL)
	return error;

    SLIST_FOREACH(cti, &cputimer_intr_head, next) {
	if (strcmp(cti->name, name) == 0)
	    break;
    }
    if (cti == NULL)
	return ENOENT;
    if (cti == sys_cputimer_intr)
	return 0;

    error = cputimer_intr_select(cti, CPUTIMER_INTR_PRIO_MAX);
    if (!error)
	cputimer_intr_restart();
    return error;
}

SYSCTL_NODE(_kern_cputimer, OID_AUTO, intr, CTLFLAG_RW, NULL,
	    "interrupt cputimer");

SYSCTL_PROC(_kern_cputimer_intr, OID_AUTO, reglist, CTLTYPE_STRING|CTLFLAG_RD,
	    NULL, 0, sysctl_cputimer_intr_reglist, "A", "");
SYSCTL_PROC(_kern_cputimer_intr, OID_AUTO, freq, CTLTYPE_INT|CTLFLAG_RD,
	    NULL, 0, sysctl_cputimer_intr_freq, "I", "");
SYSCTL_PROC(_kern_cputimer_intr, OID_AUTO, select, CTLTYPE_STRING|CTLFLAG_RW,
	    NULL, 0, sysctl_cputimer_intr_select, "A", "");
