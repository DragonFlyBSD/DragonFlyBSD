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
#include <sys/mplock2.h>

#include "acpi.h"
#include "accommon.h"
#include <dev/acpica/acpivar.h>

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
    ACPI_OSD_EXEC_CALLBACK	at_function;
    void			*at_context;
    ACPI_EXECUTE_TYPE		at_type;
};

static struct thread *acpi_task_td;
struct lwkt_port acpi_afree_rport;

/*
 * Initialize the ACPI helper thread.
 */
int
acpi_task_thread_init(void)
{
    lwkt_initport_replyonly(&acpi_afree_rport, acpi_autofree_reply);
    acpi_task_td = kmalloc(sizeof(struct thread), M_DEVBUF, M_INTWAIT | M_ZERO);
    lwkt_create(acpi_task_thread, NULL, NULL, acpi_task_td, TDF_NOSTART, 0,
	"acpi_task");
    return (0);
}

void
acpi_task_thread_schedule(void)
{
    lwkt_schedule(acpi_task_td);
}

/*
 * The ACPI helper thread processes OSD execution callback messages.
 */
static void
acpi_task_thread(void *arg)
{
    ACPI_OSD_EXEC_CALLBACK func;
    struct acpi_task *at;

    get_mplock();
    for (;;) {
	at = (void *)lwkt_waitport(&curthread->td_msgport, 0);
	func = at->at_function;
	func(at->at_context);
	lwkt_replymsg(&at->at_msg, 0);
    }
    rel_mplock();
}

/*
 * Queue an ACPI message for execution by allocating a LWKT message structure
 * and sending the message to the helper thread.  The reply port is setup
 * to automatically free the message.
 */
ACPI_STATUS
AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function,
	      void *Context)
{
    struct acpi_task	*at;

    switch (Type) {
    case OSL_GLOBAL_LOCK_HANDLER:
    case OSL_NOTIFY_HANDLER:
    case OSL_GPE_HANDLER:
    case OSL_DEBUGGER_THREAD:
    case OSL_EC_POLL_HANDLER:
    case OSL_EC_BURST_HANDLER:
	break;
    default:
	return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /* Note: Interrupt Context */
    at = kmalloc(sizeof(*at), M_ACPITASK, M_INTWAIT | M_ZERO);
    lwkt_initmsg(&at->at_msg, &acpi_afree_rport, 0);
    at->at_function = Function;
    at->at_context = Context;
    at->at_type = Type;
    lwkt_sendmsg(&acpi_task_td->td_msgport, &at->at_msg);
    return_ACPI_STATUS (AE_OK);
}

/*
 * The message's reply port just frees the message.
 */
static void
acpi_autofree_reply(lwkt_port_t port, lwkt_msg_t msg)
{
    kfree(msg, M_ACPITASK);
}

UINT64
AcpiOsGetTimer (void)
{
    struct timeval  time;

    microtime(&time);

    /* Seconds * 10^7 = 100ns(10^-7), Microseconds(10^-6) * 10^1 = 100ns */

    return (((UINT64) time.tv_sec * 10000000) + ((UINT64) time.tv_usec * 10));
}

void
AcpiOsSleep(UINT64 Milliseconds)
{
    int		timo;
    static int	dummy;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    timo = Milliseconds * hz / 1000;

    /* 
     * If requested sleep time is less than our hz resolution, or if
     * the system is in early boot before the system tick is operational,
     * use DELAY instead for better granularity.
     */
    if (clocks_running == 0) {
	while (timo > 1000000) {
	    DELAY(1000000);
	    timo -= 1000000;
	}
	if (timo)
	    DELAY(timo * 1000);
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

ACPI_THREAD_ID
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
