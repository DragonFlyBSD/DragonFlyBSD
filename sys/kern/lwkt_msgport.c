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
 * NOTE! This file may be compiled for userland libraries as well as for
 * the kernel.
 *
 * $DragonFly: src/sys/kern/lwkt_msgport.c,v 1.40 2007/05/23 08:57:04 dillon Exp $
 */

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>
#include <sys/signalvar.h>
#include <sys/signal2.h>
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
#include <machine/cpufunc.h>
#ifdef SMP
#include <machine/smp.h>
#endif

#include <sys/malloc.h>
MALLOC_DEFINE(M_LWKTMSG, "lwkt message", "lwkt message");

#else

#include <sys/stdint.h>
#include <libcaps/thread.h>
#include <sys/thread.h>
#include <sys/msgport.h>
#include <sys/errno.h>
#include <libcaps/globaldata.h>
#include <machine/cpufunc.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <string.h>

#endif /* _KERNEL */


/************************************************************************
 *				MESSAGE FUNCTIONS			*
 ************************************************************************/

#ifdef SMP
static void lwkt_replyport_remote(lwkt_msg_t msg);
static void lwkt_putport_remote(lwkt_msg_t msg);
#endif

/*
 * lwkt_sendmsg()
 *
 *	Request asynchronous completion and call lwkt_beginmsg().  The
 *	target port can opt to execute the message synchronously or
 *	asynchronously and this function will automatically queue the
 *	response if the target executes the message synchronously.
 *
 *	NOTE: The message is in an indeterminant state until this call
 *	returns.  The caller should not mess with it (e.g. try to abort it)
 *	until then.
 */
void
lwkt_sendmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    int error;

    KKASSERT(msg->ms_reply_port != NULL &&
	     (msg->ms_flags & (MSGF_DONE|MSGF_QUEUED)) == MSGF_DONE);
    msg->ms_flags &= ~(MSGF_REPLY | MSGF_SYNC | MSGF_DONE);
    if ((error = lwkt_beginmsg(port, msg)) != EASYNC) {
	lwkt_replymsg(msg, error);
    }
}

/*
 * lwkt_domsg()
 *
 *	Request asynchronous completion and call lwkt_beginmsg().  The
 *	target port can opt to execute the message synchronously or
 *	asynchronously and this function will automatically queue the
 *	response if the target executes the message synchronously.
 */
int
lwkt_domsg(lwkt_port_t port, lwkt_msg_t msg)
{
    int error;

    KKASSERT(msg->ms_reply_port != NULL &&
	     (msg->ms_flags & (MSGF_DONE|MSGF_QUEUED)) == MSGF_DONE);
    msg->ms_flags &= ~(MSGF_REPLY | MSGF_DONE);
    msg->ms_flags |= MSGF_SYNC;
    if ((error = lwkt_beginmsg(port, msg)) == EASYNC) {
	error = lwkt_waitmsg(msg);
    } else {
	msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
    }
    return(error);
}

/************************************************************************
 *				PORT FUNCTIONS				*
 ************************************************************************/

/*
 * lwkt_initport()
 *
 *	Initialize a port for use and assign it to the specified thread.
 *	The default reply function is to return the message to the originator.
 */
void
lwkt_initport(lwkt_port_t port, thread_t td)
{
    bzero(port, sizeof(*port));
    TAILQ_INIT(&port->mp_msgq);
    port->mp_td = td;
    port->mp_putport = lwkt_default_putport;
    port->mp_waitport =  lwkt_default_waitport;
    port->mp_replyport = lwkt_default_replyport;
}

/*
 * Similar to the standard initport, this function simply marks the message
 * as being done and does not attempt to return it to an originating port.
 */
void
lwkt_initport_null_rport(lwkt_port_t port, thread_t td)
{
    lwkt_initport(port, td);
    port->mp_replyport = lwkt_null_replyport;
}

/*
 * lwkt_getport()
 *
 *	Retrieve the next message from the port's message queue, return NULL
 *	if no messages are pending.  The retrieved message will either be a
 *	request or a reply based on the MSGF_REPLY bit.
 *
 *	The calling thread MUST own the port.
 */

static __inline
void
_lwkt_pullmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    /*
     * normal case, remove and return the message.
     */
    TAILQ_REMOVE(&port->mp_msgq, msg, ms_node);
    msg->ms_flags &= ~MSGF_QUEUED;
}

void *
lwkt_getport(lwkt_port_t port)
{
    lwkt_msg_t msg;

    KKASSERT(port->mp_td == curthread);

    crit_enter_quick(port->mp_td);
    if ((msg = TAILQ_FIRST(&port->mp_msgq)) != NULL)
	_lwkt_pullmsg(port, msg);
    crit_exit_quick(port->mp_td);
    return(msg);
}

#ifdef SMP

/*
 * This function completes reply processing for the default case in the
 * context of the originating cpu.
 */
static
void
lwkt_replyport_remote(lwkt_msg_t msg)
{
    lwkt_port_t port = msg->ms_reply_port;

#ifdef INVARIANTS
    KKASSERT(msg->ms_flags & MSGF_INTRANSIT);
    msg->ms_flags &= ~MSGF_INTRANSIT;
#endif
    TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
    msg->ms_flags |= MSGF_REPLY | MSGF_DONE | MSGF_QUEUED;
    if (port->mp_flags & MSGPORTF_WAITING)
	lwkt_schedule(port->mp_td);
}

#endif

/*
 * lwkt_default_replyport() - Backend to lwkt_replymsg()
 *
 * Called with the reply port as an argument but in the context of the
 * original target port.
 *
 * The critical section protects us from IPIs on the this CPU.
 */
void
lwkt_default_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    KKASSERT((msg->ms_flags & (MSGF_DONE|MSGF_QUEUED)) == 0);

    crit_enter();
    if (msg->ms_flags & MSGF_SYNC) {
	/*
	 * If a synchronous completion has been requested, just wakeup
	 * the message without bothering to queue it to the target port.
	 */
	msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
	if (port->mp_flags & MSGPORTF_WAITING)
	    lwkt_schedule(port->mp_td);
    } else {
	/*
	 * If an asynchronous completion has been requested the message
	 * must be queued to the reply port.  MSGF_REPLY cannot be set
	 * until the message actually gets queued.
	 */
#ifdef SMP
	if (port->mp_td->td_gd == mycpu) {
#endif
	    TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	    msg->ms_flags |= MSGF_REPLY | MSGF_DONE | MSGF_QUEUED;
	    if (port->mp_flags & MSGPORTF_WAITING)
		lwkt_schedule(port->mp_td);
#ifdef SMP
	} else {
#ifdef INVARIANTS
	    msg->ms_flags |= MSGF_INTRANSIT;
#endif
	    msg->ms_flags |= MSGF_REPLY;
	    lwkt_send_ipiq(port->mp_td->td_gd,
			   (ipifunc1_t)lwkt_replyport_remote, msg);
	}
#endif
    }
    crit_exit();
}

/*
 * You can point a port's reply vector at this function if you just want
 * the message marked done, without any queueing or signaling.  This is
 * often used for structure-embedded messages.
 */
void
lwkt_null_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
}

/*
 * lwkt_default_putport() - Backend to lwkt_beginmsg()
 *
 * Called with the target port as an argument but in the context of the
 * reply port.  This function always implements an asynchronous put to
 * the target message port, and thus returns EASYNC.
 *
 * The message must already have cleared MSGF_DONE and MSGF_REPLY
 */

#ifdef SMP

static
void
lwkt_putport_remote(lwkt_msg_t msg)
{
    lwkt_port_t port = msg->ms_target_port;

#ifdef INVARIANTS
    KKASSERT(msg->ms_flags & MSGF_INTRANSIT);
    msg->ms_flags &= ~MSGF_INTRANSIT;
#endif
    TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
    msg->ms_flags |= MSGF_QUEUED;
    if (port->mp_flags & MSGPORTF_WAITING)
	lwkt_schedule(port->mp_td);
}

#endif

int
lwkt_default_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    KKASSERT((msg->ms_flags & (MSGF_DONE | MSGF_REPLY)) == 0);

    msg->ms_target_port = port;
    crit_enter();
#ifdef SMP
    if (port->mp_td->td_gd == mycpu) {
#endif
	msg->ms_flags |= MSGF_QUEUED;
	TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	if (port->mp_flags & MSGPORTF_WAITING)
	    lwkt_schedule(port->mp_td);
#ifdef SMP
    } else {
#ifdef INVARIANTS
	msg->ms_flags |= MSGF_INTRANSIT;
#endif
	lwkt_send_ipiq(port->mp_td->td_gd,
			(ipifunc1_t)lwkt_putport_remote, msg);
    }
#endif
    crit_exit();
    return (EASYNC);
}

/*
 * lwkt_forwardmsg()
 *
 * Forward a message received on one port to another port.
 */
int
lwkt_forwardmsg(lwkt_port_t port, lwkt_msg_t msg)
{   
    int error;

    crit_enter();
    KKASSERT((msg->ms_flags & (MSGF_QUEUED|MSGF_DONE|MSGF_REPLY)) == 0);
    if ((error = port->mp_putport(port, msg)) != EASYNC)
	lwkt_replymsg(msg, error);
    crit_exit();
    return(error);
}

/*
 * lwkt_abortmsg()
 *
 * Attempt to abort a message.  This only works if MSGF_ABORTABLE is set.
 * The caller must ensure that the message will not be both replied AND
 * destroyed while the abort is in progress.
 *
 * This function issues a callback which might block!
 */

void
lwkt_abortmsg(lwkt_msg_t msg)
{
    /*
     * A critical section protects us from reply IPIs on this cpu.
     */
    crit_enter();

    /*
     * Shortcut the operation if the message has already been returned.
     * The callback typically constructs a lwkt_msg with the abort request,
     * issues it synchronously, and waits for completion.  The callback
     * is not required to actually abort the message and the target port,
     * upon receiving an abort request message generated by the callback
     * should check whether the original message has already completed or
     * not.
     */
    if (msg->ms_flags & MSGF_ABORTABLE) {
	if ((msg->ms_flags & (MSGF_DONE|MSGF_REPLY)) == 0)
	    msg->ms_abortfn(msg);
    }
    crit_exit();
}

/*
 * lwkt_default_waitport()
 *
 *	If msg is NULL, dequeue the next message from the port's message
 *	queue, block until a message is ready.  This function never
 *	returns NULL.
 *
 *	If msg is non-NULL, block until the requested message has been
 *	replied, then dequeue and return it.
 *
 *	NOTE: This function should not be used to wait for specific
 *	incoming requests because MSGF_DONE only applies to replies.
 *
 *	Note that the API does not currently support multiple threads waiting
 * 	on a single port.  The port must be owned by the caller.
 */
void *
lwkt_default_waitport(lwkt_port_t port, lwkt_msg_t msg)
{
    thread_t td = curthread;
    int sentabort;

    KKASSERT(port->mp_td == td);
    crit_enter_quick(td);
    if (msg == NULL) {
	/*
	 * Wait for any message
	 */
	if ((msg = TAILQ_FIRST(&port->mp_msgq)) == NULL) {
	    port->mp_flags |= MSGPORTF_WAITING;
	    td->td_flags |= TDF_BLOCKED;
	    do {
		lwkt_deschedule_self(td);
		lwkt_switch();
	    } while ((msg = TAILQ_FIRST(&port->mp_msgq)) == NULL);
	    td->td_flags &= ~TDF_BLOCKED;
	    port->mp_flags &= ~MSGPORTF_WAITING;
	}
	_lwkt_pullmsg(port, msg);
    } else {
	/*
	 * Wait for a specific message.
	 */
	KKASSERT(msg->ms_reply_port == port);
	if ((msg->ms_flags & MSGF_DONE) == 0) {
	    sentabort = 0;
	    while ((msg->ms_flags & MSGF_DONE) == 0) {
		/*
		 * MSGF_PCATCH is only set by processes which wish to
		 * abort the message they are blocked on when a signal
		 * occurs.  Note that we still must wait for message
		 * completion after sending an abort request.
		 */
		if (msg->ms_flags & MSGF_PCATCH) {
		    if (sentabort == 0 && CURSIG(port->mp_td->td_lwp)) {
			sentabort = 1;
			lwkt_abortmsg(msg);
			continue;
		    }
		}

		/*
		 * XXX set TDF_SINTR so 'ps' knows the difference between
		 * an interruptable wait and a disk wait.  YYY eventually
		 * move LWP_SINTR to TDF_SINTR to reduce duplication.
		 */
		port->mp_flags |= MSGPORTF_WAITING;
		td->td_flags |= TDF_SINTR | TDF_BLOCKED;
		lwkt_deschedule_self(td);
		lwkt_switch();
		td->td_flags &= ~(TDF_SINTR | TDF_BLOCKED);
		port->mp_flags &= ~MSGPORTF_WAITING;
	    }
	}

	/*
	 * Once the MSGF_DONE bit is set, the message is stable.  We
	 * can just check MSGF_QUEUED to determine
	 */
	if (msg->ms_flags & MSGF_QUEUED) {
	    msg->ms_flags &= ~MSGF_QUEUED;
	    TAILQ_REMOVE(&port->mp_msgq, msg, ms_node);
	}
    }
    crit_exit_quick(td);
    return(msg);
}

