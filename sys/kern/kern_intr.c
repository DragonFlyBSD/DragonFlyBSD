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
 * $DragonFly: src/sys/kern/kern_intr.c,v 1.6 2003/06/30 19:50:31 dillon Exp $
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

static void ithread_handler(void *arg);

void
register_swi(int intr, inthand2_t *handler, void *arg, const char *name)
{
    if (intr < NHWI || intr >= NHWI + NSWI)
	panic("register_swi: bad intr %d", intr);
    register_int(intr, handler, arg, name);
}

void
register_int(int intr, inthand2_t *handler, void *arg, const char *name)
{
    intrec_t **list;
    intrec_t *rec;
    thread_t td;

    if (intr < 0 || intr > NHWI + NSWI)
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
	    &ithread_ary[intr], TDF_STOPREQ, "ithread %d", intr);
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

/*
 * Dispatch an interrupt.  If there's nothing to do we have a stray
 * interrupt and can just return, leaving the interrupt masked.
 */
void
sched_ithd(int intr)
{
    thread_t td;

    if ((td = ithreads[intr]) != NULL) {
	if (intlists[intr] == NULL) {
	    printf("sched_ithd: stray interrupt %d\n", intr);
	} else {
	    lwkt_schedule(td);
	    lwkt_preempt(td, intr);
	}
    } else {
	printf("sched_ithd: stray interrupt %d\n", intr);
    }
}

/*
 * Interrupt threads run this as their main loop.  The handler should be
 * in a critical section on entry.
 */
static void
ithread_handler(void *arg)
{
    int intr = (int)arg;
    intrec_t **list = &intlists[intr];
    intrec_t *rec;
    intrec_t *nrec;

    KKASSERT(curthread->td_pri >= TDPRI_CRIT);
    for (;;) {
	for (rec = *list; rec; rec = nrec) {
	    nrec = rec->next;
	    rec->handler(rec->argument);
	}
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
