/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/kern/lwkt_msgport.c,v 1.6 2003/10/22 01:01:16 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>
#include <machine/cpu.h>
#include <sys/lock.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <machine/stdarg.h>
#include <machine/ipl.h>
#ifdef SMP
#include <machine/smp.h>
#endif


/************************************************************************
 *				MESSAGE FUNCTIONS			*
 ************************************************************************/

static void lwkt_replyport_remote(lwkt_msg_t msg);
static void lwkt_putport_remote(lwkt_msg_t msg);
static void lwkt_abortport_remote(lwkt_port_t port);

void
lwkt_initmsg_td(lwkt_msg_t msg, thread_t td)
{
    lwkt_initmsg(msg, &td->td_msgport, 0);
}

/*
 * lwkt_sendmsg()
 *
 *	Send a message asynchronously.  This function requests asynchronous
 *	completion and calls lwkt_beginmsg().  If the target port decides to
 *	run the message synchronously this function will automatically queue
 *	the message to the current thread's message queue to present a
 *	consistent interface to the caller. 
 *
 *	The message's ms_cmd must be initialized and its ms_flags must be
 *	at least zero'd out.  lwkt_sendmsg() will initialize the message's
 *	reply port to the current thread's built-in reply port.
 */
void
lwkt_sendmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    int error;

    msg->ms_flags |= MSGF_ASYNC;
    msg->ms_flags &= ~(MSGF_REPLY | MSGF_QUEUED);
    msg->ms_reply_port = &curthread->td_msgport;
    msg->ms_abortreq = 0;
    if ((error = lwkt_beginmsg(port, msg)) != EASYNC) {
	lwkt_replymsg(msg, error);
    }
}

/*
 * lwkt_domsg()
 *
 *	Send a message synchronously.  This function requests synchronous
 *	completion and calls lwkt_beginmsg().  If the target port decides to
 *	run the message asynchronously this function will block waiting for
 *	the message to complete.  Since MSGF_ASYNC is not set the target
 *	will not attempt to queue the reply to a reply port but will simply
 *	wake up anyone waiting on the message.
 *
 *	A synchronous error code is always returned.
 *
 *	The message's ms_cmd must be initialized and its ms_flags must be
 *	at least zero'd out.  lwkt_domsg() will initialize the message's
 *	reply port to the current thread's built-in reply port.
 */
int
lwkt_domsg(lwkt_port_t port, lwkt_msg_t msg)
{
    int error;

    msg->ms_flags &= ~(MSGF_ASYNC | MSGF_REPLY | MSGF_QUEUED);
    msg->ms_reply_port = &curthread->td_msgport;
    msg->ms_abortreq = 0;
    if ((error = lwkt_beginmsg(port, msg)) == EASYNC) {
	error = lwkt_waitmsg(msg);
    }
    return(error);
}

/*
 * lwkt_waitmsg()
 *
 *	Wait for a message that we originated to complete, remove it from
 *	the reply queue if necessary, and return its error code.
 *
 *	This call may be used in virtually any situation, including for the
 *	case where you used lwkt_sendmsg() to initiate the message.
 *
 *	Note that we don't own the message any more so we cannot safely 
 *	modify ms_flags, meaning we can't clear MSGF_ASYNC as an optimization.
 *	However, since any remote cpu replying to the message will IPI the
 *	message over to us for action, a critical section is sufficient to
 *	protect td_msgq.
 */
int
lwkt_waitmsg(lwkt_msg_t msg)
{
    lwkt_port_t port;

    /*
     * Done but not queued case (message was originally a synchronous request)
     */
    if ((msg->ms_flags & (MSGF_DONE|MSGF_REPLY)) == MSGF_DONE)
	return(msg->ms_error);

    port = msg->ms_reply_port;
    KKASSERT(port->mp_td == curthread);	/* for now */
    crit_enter();
    if ((msg->ms_flags & MSGF_DONE) == 0) {
	port->mp_flags |= MSGPORTF_WAITING;
	do {
	    lwkt_deschedule_self();
	    lwkt_switch();
	} while ((msg->ms_flags & MSGF_DONE) == 0);
	port->mp_flags &= ~MSGPORTF_WAITING;
    }
    /*
     * We own the message now.
     */
    if (msg->ms_flags & MSGF_QUEUED) {
	msg->ms_flags &= ~MSGF_QUEUED;
	TAILQ_REMOVE(&port->mp_msgq, msg, ms_node);
    }
    crit_exit();
    return(msg->ms_error);
}


/************************************************************************
 *				PORT FUNCTIONS				*
 ************************************************************************/

/*
 * lwkt_initport()
 *
 *	Initialize a port for use and assign it to the specified thread.
 */
void
lwkt_init_port(lwkt_port_t port, thread_t td)
{
    bzero(port, sizeof(*port));
    TAILQ_INIT(&port->mp_msgq);
    port->mp_td = td;
    port->mp_beginmsg = lwkt_putport;
    port->mp_abortmsg =  lwkt_abortport;
    port->mp_returnmsg = lwkt_replyport;
}

/*
 * lwkt_replyport()
 *
 *	This function is typically assigned to the mp_replymsg port vector.
 *
 *	The message is being returned to the specified port.  The port is
 *	owned by the mp_td thread.  If we are on the same cpu as the mp_td
 *	thread we can trivially queue the message to the messageq, otherwise
 *	we have to send an ipi message to the correct cpu.   We then schedule
 *	the target thread.
 *
 *	If MSGF_ASYNC is not set we do not bother queueing the message, we
 *	just set the DONE bit.  
 *
 *	Note that the IPIQ callback function (*_remote) is entered with a
 *	critical section already held.
 */

static __inline
void
_lwkt_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    thread_t td = port->mp_td;

    if (td->td_gd == mycpu) {
	TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	msg->ms_flags |= MSGF_DONE | MSGF_REPLY | MSGF_QUEUED;
	if (port->mp_flags & MSGPORTF_WAITING)
	    lwkt_schedule(td);
    } else {
	lwkt_send_ipiq(td->td_gd->gd_cpuid, (ipifunc_t)lwkt_replyport_remote, msg);
    }
}

static
void
lwkt_replyport_remote(lwkt_msg_t msg)
{
    _lwkt_replyport(msg->ms_reply_port, msg);
}


void
lwkt_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    crit_enter();
    if (msg->ms_flags & MSGF_ASYNC) {
	_lwkt_replyport(port, msg);
    } else {
	msg->ms_flags |= MSGF_DONE;
	if (port->mp_flags & MSGPORTF_WAITING)
	    lwkt_schedule(port->mp_td);
    }
    crit_exit();
}

/*
 * lwkt_putport()
 *
 *	This function is typically assigned to the mp_beginmsg port vector.
 *
 *	Queue a message to the target port and wakeup the thread owning it.
 *	This function always returns EASYNC and may be assigned to a
 *	message port's mp_beginmsg function vector.
 */
static
__inline
void
_lwkt_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    thread_t td = port->mp_td;

    if (td->td_gd == mycpu) {
	TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	msg->ms_flags |= MSGF_QUEUED;
	if (port->mp_flags & MSGPORTF_WAITING)
	    lwkt_schedule(td);
    } else {
	msg->ms_target_port = port;
	lwkt_send_ipiq(td->td_gd->gd_cpuid, (ipifunc_t)lwkt_putport_remote, msg);
    }
}

static
void
lwkt_putport_remote(lwkt_msg_t msg)
{
    _lwkt_putport(msg->ms_target_port, msg);
}

int
lwkt_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    crit_enter();
    msg->ms_flags &= ~MSGF_DONE;
    _lwkt_putport(port, msg);
    crit_exit();
    return(EASYNC);
}

/*
 * lwkt_abortport()
 *
 *	This function is typically assigned to the mp_abortmsg port vector.
 *
 *	This function attempts to abort a message.  Aborts are always 
 *	optional, so we could just do nothing if we wanted.  We get onto the
 *	cpu owning the port, check to see if the message is queued on the 
 *	port's message queue, and remove and abort it if it is.  Otherwise
 *	we do nothing.  
 *
 *	Note that we cannot safely use ms_target_port because the port might
 *	have forwarded the message on to some other port and we could race
 *	against that use of ms_target_port.
 */
static
__inline
void
_lwkt_abortport(lwkt_port_t port)
{
    thread_t td = port->mp_td;
    lwkt_msg_t msg;

    if (td->td_gd == mycpu) {
again:
	msg = TAILQ_FIRST(&port->mp_msgq);
	while (msg) {
	    if (msg->ms_abortreq) {
		TAILQ_REMOVE(&port->mp_msgq, msg, ms_node);
		msg->ms_flags &= ~MSGF_QUEUED;
		lwkt_replymsg(msg, ECONNABORTED); /* YYY dangerous from IPI? */
		goto again;
	    }
	    msg = TAILQ_NEXT(msg, ms_node);
	}
    } else {
	lwkt_send_ipiq(td->td_gd->gd_cpuid, (ipifunc_t)lwkt_abortport_remote, port);
    }
}

static
void
lwkt_abortport_remote(lwkt_port_t port)
{
    _lwkt_abortport(port);
}

void
lwkt_abortport(lwkt_port_t port, lwkt_msg_t msg)
{
    msg->ms_abortreq = 1;
    crit_enter();
    _lwkt_abortport(port);
    crit_exit();
}

/*
 * lwkt_getport()
 *
 *	Retrieve the next message from the port's message queue, return NULL
 *	if no messages are pending.
 *
 *	The calling thread MUST own the port.
 */
void *
lwkt_getport(lwkt_port_t port)
{
    lwkt_msg_t msg;

    KKASSERT(port->mp_td == curthread);

    crit_enter();
    if ((msg = TAILQ_FIRST(&port->mp_msgq)) != NULL) {
	TAILQ_REMOVE(&port->mp_msgq, msg, ms_node);
	msg->ms_flags &= ~MSGF_QUEUED;
    }
    crit_exit();
    return(msg);
}

/*
 * lwkt_waitport()
 *
 *	Retrieve the next message from the port's message queue, block until
 *	a message is ready.  This function never returns NULL.
 */
void *
lwkt_waitport(lwkt_port_t port)
{
    lwkt_msg_t msg;

    KKASSERT(port->mp_td == curthread);

    crit_enter();
    if ((msg = TAILQ_FIRST(&port->mp_msgq)) == NULL) {
	port->mp_flags |= MSGPORTF_WAITING;
	do {
	    lwkt_deschedule_self();
	    lwkt_switch();
	} while ((msg = TAILQ_FIRST(&port->mp_msgq)) == NULL);
	port->mp_flags &= ~MSGPORTF_WAITING;
    }
    TAILQ_REMOVE(&port->mp_msgq, msg, ms_node);
    msg->ms_flags &= ~MSGF_QUEUED;
    crit_exit();
    return(msg);
}

