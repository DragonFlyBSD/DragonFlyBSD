/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/acpica/Osd/OsdSchedule.c,v 1.28 2004/05/06 02:18:58 njl Exp $
 * $DragonFly: src/sys/dev/acpica5/Osd/OsdSchedule.c,v 1.4 2004/08/02 19:51:09 dillon Exp $
 */

/*
 * 6.3 : Scheduling services
 */

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/msgport.h>
#include <sys/taskqueue.h>
#include <machine/clock.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

#include "acpi.h"
#include <dev/acpica5/acpivar.h>

#define _COMPONENT	ACPI_OS_SERVICES
ACPI_MODULE_NAME("SCHEDULE")

/*
 * This is a little complicated due to the fact that we need to build and then
 * free a 'struct task' for each task we enqueue.
 */

MALLOC_DEFINE(M_ACPITASK, "acpitask", "ACPI deferred task");

static void	acpi_task_thread(void *arg);
static void	acpi_autofree_reply(lwkt_port_t port, lwkt_msg_t msg);

struct acpi_task {
    struct lwkt_msg		at_msg;
    OSD_EXECUTION_CALLBACK	at_function;
    void			*at_context;
    int				at_priority;
};

static struct thread *acpi_task_td;
struct lwkt_port acpi_afree_rport;

/*
 * Initialize the ACPI helper thread.
 */
int
acpi_task_thread_init(void)
{
    lwkt_initport(&acpi_afree_rport, NULL);
    acpi_afree_rport.mp_replyport = acpi_autofree_reply;
    kthread_create(acpi_task_thread, NULL, &acpi_task_td,
			0, 0, "acpi_task");
    return (0);
}

/*
 * The ACPI helper thread processes OSD execution callback messages.
 */
static void
acpi_task_thread(void *arg)
{
    OSD_EXECUTION_CALLBACK func;
    struct acpi_task *at;

    for (;;) {
	at = (void *)lwkt_waitport(&curthread->td_msgport, NULL);
	func = (OSD_EXECUTION_CALLBACK)at->at_function;
	func((void *)at->at_context);
	lwkt_replymsg(&at->at_msg, 0);
    }
    kthread_exit();
}

/*
 * Queue an ACPI message for execution by allocating a LWKT message structure
 * and sending the message to the helper thread.  The reply port is setup
 * to automatically free the message.
 */
ACPI_STATUS
AcpiOsQueueForExecution(UINT32 Priority, OSD_EXECUTION_CALLBACK Function,
    void *Context)
{
    struct acpi_task	*at;
    int pri;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    if (Function == NULL)
	return_ACPI_STATUS (AE_BAD_PARAMETER);

    switch (Priority) {
    case OSD_PRIORITY_GPE:
	pri = 4;
	break;
    case OSD_PRIORITY_HIGH:
	pri = 3;
	break;
    case OSD_PRIORITY_MED:
	pri = 2;
	break;
    case OSD_PRIORITY_LO:
	pri = 1;
	break;
    default:
	return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /* Note: Interrupt Context */
    at = malloc(sizeof(*at), M_ACPITASK, M_INTWAIT | M_ZERO);
    lwkt_initmsg(&at->at_msg, &acpi_afree_rport, 0, 
		lwkt_cmd_op_none, lwkt_cmd_op_none);
    at->at_function = Function;
    at->at_context = Context;
    at->at_priority = pri;
    lwkt_sendmsg(&acpi_task_td->td_msgport, &at->at_msg);
    return_ACPI_STATUS (AE_OK);
}

/*
 * The message's reply port just frees the message.
 */
static void
acpi_autofree_reply(lwkt_port_t port, lwkt_msg_t msg)
{
    free(msg, M_ACPITASK);
}


void
AcpiOsSleep(UINT32 Seconds, UINT32 Milliseconds)
{
    int		timo;
    static int	dummy;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    timo = (Seconds * hz) + Milliseconds * hz / 1000;

    /* 
     * If requested sleep time is less than our hz resolution, or if
     * the system is in early boot before the system tick is operational,
     * use DELAY instead for better granularity.
     */
    if (clocks_running == 0) {
	while (Seconds) {
	    DELAY(1000000);
	    --Seconds;
	}
	if (Milliseconds)
	    DELAY(Milliseconds * 1000);
    } else if (timo > 0) {
	tsleep(&dummy, 0, "acpislp", timo);
    } else {
	DELAY(Milliseconds * 1000);
    }
    return_VOID;
}

void
AcpiOsStall(UINT32 Microseconds)
{
    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    DELAY(Microseconds);
    return_VOID;
}

UINT32
AcpiOsGetThreadId(void)
{
    struct proc *p;

    /* XXX do not add ACPI_FUNCTION_TRACE here, results in recursive call. */

    p = curproc;
    if (p == NULL)
	p = &proc0;
    KASSERT(p != NULL, ("%s: curproc is NULL!", __func__));

    /* Returning 0 is not allowed. */
    return (p->p_pid + 1);
}
