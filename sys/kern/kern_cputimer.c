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
 * $DragonFly: src/sys/kern/kern_cputimer.c,v 1.1 2005/06/01 20:19:47 dillon Exp $
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
    NULL,
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

static struct cputimer *cputimer_reg_base = &dummy_cputimer;
struct cputimer *sys_cputimer = &dummy_cputimer;

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
	}
    }
}

/*
 * Register a timer.  If the timer has already been registered, do nothing.
 */
void
cputimer_register(struct cputimer *timer)
{
    struct cputimer **scanpp;
    struct cputimer *scan;

    for (scanpp = &cputimer_reg_base; 
	 (scan = *scanpp) != NULL;
	 scanpp = &scan->next
    ) {
	if (scan == timer)
	    return;
    }
    timer->next = NULL;
    *scanpp = timer;
}

/*
 * Deregister a timer.  If the timer has already been deregistered, do nothing.
 */
void
cputimer_deregister(struct cputimer *timer)
{
    struct cputimer **scanpp;
    struct cputimer *scan;
    struct cputimer *best;

    /*
     * Locate and remove the timer.  If the timer is our currently active
     * timer, revert to the dummy timer.
     */
    scanpp = &cputimer_reg_base; 
    while ((scan = *scanpp) != NULL) {
	if (scan == timer) {
	    *scanpp = timer->next;
	    if (timer == sys_cputimer)
		cputimer_select(&dummy_cputimer, 0x7FFFFFFF);
	} else {
	    scanpp = &scan->next;
	}
    }

    /*
     * If sys_cputimer reverted to the dummy, select the best one
     */
    if (sys_cputimer == &dummy_cputimer) {
	best = NULL;
	for (scan = cputimer_reg_base; scan; scan = scan->next) {
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
cputimer_set_frequency(struct cputimer *timer, int freq)
{
    timer->freq = freq;
    timer->freq64_usec = (1000000LL << 32) / freq;
    timer->freq64_nsec = (1000000000LL << 32) / freq;
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
 ************************************************************************/

static int
sysctl_cputimer_reglist(SYSCTL_HANDLER_ARGS)
{
    struct cputimer *scan;
    struct cputimer *selected;
    int error = 0;
    int loop = 0;
    char buf[32];

    /*
     * Writing to the reglist selects a timer
     */
    if (req->newptr != NULL) {
	if (req->newlen < 0 || req->newlen >= sizeof(buf))
	    return (ENAMETOOLONG);
	error = SYSCTL_IN(req, buf, req->newlen);
	buf[req->newlen] = 0;
    } else {
	buf[0] = 0;
    }

    /*
     * Build a list of available timers
     */
    selected = NULL;
    for (scan = cputimer_reg_base; scan; scan = scan->next) {
	if (error == 0 && loop)
	    error = SYSCTL_OUT(req, " ", 1);
	if (error == 0)
	    error = SYSCTL_OUT(req, scan->name, strlen(scan->name));
	if (buf[0] && strcmp(buf, scan->name) == 0)
	    selected = scan;
	++loop;
    }

    /*
     * If a particular timer is being selected by the sysctl, switch to it.
     */
    if (selected)
	cputimer_select(selected, 0x7FFFFFFF);
    if (error == 0 && buf[0] && selected == NULL)
	error = ENOENT;
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

SYSCTL_PROC(_kern_cputimer, OID_AUTO, select, CTLTYPE_STRING|CTLFLAG_RW,
	    NULL, NULL, sysctl_cputimer_reglist, "A", "");
SYSCTL_PROC(_kern_cputimer, OID_AUTO, name, CTLTYPE_STRING|CTLFLAG_RD,
	    NULL, NULL, sysctl_cputimer_name, "A", "");
SYSCTL_PROC(_kern_cputimer, OID_AUTO, clock, CTLTYPE_UINT|CTLFLAG_RD,
	    NULL, NULL, sysctl_cputimer_clock, "IU", "");
SYSCTL_PROC(_kern_cputimer, OID_AUTO, freq, CTLTYPE_INT|CTLFLAG_RD,
	    NULL, NULL, sysctl_cputimer_freq, "I", "");


