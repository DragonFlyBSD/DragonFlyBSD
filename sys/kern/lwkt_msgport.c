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
 * NOTE! This file may be compiled for userland libraries as well as for
 * the kernel.
 *
 * $DragonFly: src/sys/kern/lwkt_msgport.c,v 1.18 2004/04/10 20:55:23 dillon Exp $
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

#include <sys/malloc.h>
MALLOC_DEFINE(M_LWKTMSG, "lwkt message", "lwkt message");

#else

#include <sys/stdint.h>
#include <libcaps/thread.h>
#include <sys/thread.h>
#include <sys/msgport.h>
#include <sys/errno.h>
#include <libcaps/globaldata.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <string.h>

#endif /* _KERNEL */


/************************************************************************
 *				MESSAGE FUNCTIONS			*
 ************************************************************************/

static void lwkt_replyport_remote(lwkt_msg_t msg);
static void lwkt_putport_remote(lwkt_msg_t msg);

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
    KKASSERT(msg->ms_reply_port != NULL);
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
    KKASSERT(msg->ms_reply_port != NULL);
    if ((error = lwkt_beginmsg(port, msg)) == EASYNC) {
	error = lwkt_waitmsg(msg);
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
    port->mp_abortport = lwkt_default_abortport;
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
 * lwkt_default_replyport()
 *
 *	This function is typically assigned to the mp_replyport port vector.
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
 *	This inline must be entered with a critical section already held.
 *	Note that the IPIQ callback function (*_remote) is entered with a
 *	critical section already held, and we obtain one in lwkt_replyport().
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
	lwkt_send_ipiq(td->td_gd, (ipifunc_t)lwkt_replyport_remote, msg);
    }
}

static
void
lwkt_replyport_remote(lwkt_msg_t msg)
{
    _lwkt_replyport(msg->ms_reply_port, msg);
}

void
lwkt_default_replyport(lwkt_port_t port, lwkt_msg_t msg)
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
 * lwkt_default_putport()
 *
 *	This function is typically assigned to the mp_putport port vector.
 *
 *	Queue a message to the target port and wakeup the thread owning it.
 *	This function always returns EASYNC and may be assigned to a
 *	message port's mp_putport function vector.
 *
 *	You must already be in a critical section when calling
 *	the inline function.  The _remote function will be in a critical
 *	section due to being called from the IPI, and lwkt_default_putport() 
 *	enters a critical section.
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
	lwkt_send_ipiq(td->td_gd, (ipifunc_t)lwkt_putport_remote, msg);
    }
}

static
void
lwkt_putport_remote(lwkt_msg_t msg)
{
    _lwkt_putport(msg->ms_target_port, msg);
}

int
lwkt_default_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    crit_enter();
    msg->ms_flags &= ~MSGF_DONE;
    _lwkt_putport(port, msg);
    crit_exit();
    return(EASYNC);
}

/*
 * lwkt_default_abortport()
 *
 *	This function is typically assigned to the mp_abortport port vector.
 *
 *	This vector is typically called via the message's ms_target_port
 *	pointer.  It should be noted that ms_target_port may race against
 *	a forwarding operation run on a different cpu.  Any implementation
 *	of lwkt_abortport() must deal with potential races by following
 *	the message to the next appropriate port.
 *
 *	This function is a NOP.  by defaults message ports have no abort
 *	capabilities.  Remember that aborts are always optional so doing 
 *	nothing is perfectly reasonable.
 */
void
lwkt_default_abortport(lwkt_port_t port, lwkt_msg_t msg)
{
    /* NOP */
}

/*
 * lwkt_default_waitport()
 *
 *	If msg is NULL, dequeue the next message from the port's message
 *	queue, block until a message is ready.  This function never
 *	returns NULL.
 *
 *	If msg is non-NULL, block until the requested message has been returned
 *	to the port then dequeue and return it.
 *
 *	Note that the API does not currently support multiple threads waiting
 * 	on a port.  By virtue of owning the port it is controlled by our
 *	cpu and we can safely manipulate it's contents.
 */
void *
lwkt_default_waitport(lwkt_port_t port, lwkt_msg_t msg)
{
    thread_t td = curthread;

    KKASSERT(port->mp_td == td);
    crit_enter_quick(td);
    if (msg == NULL) {
	if ((msg = TAILQ_FIRST(&port->mp_msgq)) == NULL) {
	    port->mp_flags |= MSGPORTF_WAITING;
	    do {
		lwkt_deschedule_self(td);
		lwkt_switch();
	    } while ((msg = TAILQ_FIRST(&port->mp_msgq)) == NULL);
	    port->mp_flags &= ~MSGPORTF_WAITING;
	}
	TAILQ_REMOVE(&port->mp_msgq, msg, ms_node);
	msg->ms_flags &= ~MSGF_QUEUED;
    } else {
	/*
	 * If the message is marked done by not queued it has already been
	 * pulled off the port and returned and we do not have to do anything.
	 * Otherwise we do not own the message have to wait for message
	 * completion.  Beware of cpu races if MSGF_DONE is not foudn to be
	 * set!
	 */
	if ((msg->ms_flags & (MSGF_DONE|MSGF_REPLY)) != MSGF_DONE) {
	    /*
	     * We must own the reply port to safely mess with it's contents.
	     */
	    port = msg->ms_reply_port;
	    KKASSERT(port->mp_td == curthread);

	    if ((msg->ms_flags & MSGF_DONE) == 0) {
		port->mp_flags |= MSGPORTF_WAITING; /* saved by the BGL */
		do {
		    lwkt_deschedule_self(td);
		    lwkt_switch();
		} while ((msg->ms_flags & MSGF_DONE) == 0);
		port->mp_flags &= ~MSGPORTF_WAITING; /* saved by the BGL */
	    }
	    /*
	     * We own the message now.
	     */
	    if (msg->ms_flags & MSGF_QUEUED) {
		msg->ms_flags &= ~MSGF_QUEUED;
		TAILQ_REMOVE(&port->mp_msgq, msg, ms_node);
	    }
	}
    }
    crit_exit_quick(td);
    return(msg);
}

