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
 * $DragonFly: src/sys/kern/lwkt_msgport.c,v 1.30 2004/09/10 18:23:55 dillon Exp $
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
 *	The message's ms_cmd must be initialized and its ms_flags must
 *	be zero'd out.  lwkt_sendmsg() will initialize the ms_abort_port
 *	(abort chasing port).  If abort is supported, ms_abort must also be
 *	initialized.
 *
 *	NOTE: you cannot safely request an abort until lwkt_sendmsg() returns
 *	to the caller.
 *
 *	NOTE: MSGF_DONE is left set.  The target port must clear it if the
 *	message is to be handled asynchronously, while the synchronous case
 *	can just ignore it.
 */
void
lwkt_sendmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    int error;

    msg->ms_flags |= MSGF_ASYNC;
    msg->ms_flags &= ~(MSGF_REPLY1 | MSGF_REPLY2 | MSGF_QUEUED | \
			MSGF_ABORTED | MSGF_RETRIEVED);
    KKASSERT(msg->ms_reply_port != NULL);
    msg->ms_abort_port = msg->ms_reply_port;
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
 *	The message's ms_cmd must be initialized, and its ms_flags must be
 *	at least zero'd out.  lwkt_domsg() will initialize the message's
 *	ms_abort_port (abort chasing port).  If abort is supported, ms_abort
 *	must also be initialized.
 *
 *	NOTE: you cannot safely request an abort until lwkt_domsg() blocks.
 *	XXX this probably needs some work.
 *
 *	NOTE: MSGF_DONE is left set.  The target port must clear it if the
 *	message is to be handled asynchronously, while the synchronous case
 *	can just ignore it.
 */
int
lwkt_domsg(lwkt_port_t port, lwkt_msg_t msg)
{
    int error;

    msg->ms_flags &= ~(MSGF_ASYNC | MSGF_REPLY1 | MSGF_REPLY2 | \
			MSGF_QUEUED | MSGF_ABORTED | MSGF_RETRIEVED);
    KKASSERT(msg->ms_reply_port != NULL);
    msg->ms_abort_port = msg->ms_reply_port;
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
 *	if no messages are pending.  Note that callers CANNOT use the
 *	MSGF_ABORTED flag as a litmus test to determine if a message
 *	was aborted.  The flag only indicates that an abort was requested.
 *	The message's error code will indicate whether an abort occured
 *	(typically by returning EINTR).
 *
 *	Note that once a message has been dequeued it is subject to being
 *	requeued via an IPI based abort request if it is not marked MSGF_DONE.
 *
 *	If the message has been aborted we have to guarentee that abort 
 *	semantics are properly followed.   The target port will always see
 *	the original message at least once, and if it does not reply the 
 *	message before looping on its message port again it will then see
 *	the message again with ms_cmd set to ms_abort.
 *
 *	The calling thread MUST own the port.
 */

static __inline
void
_lwkt_pullmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    if ((msg->ms_flags & MSGF_ABORTED) == 0) {
	/*
	 * normal case, remove and return the message.
	 */
	TAILQ_REMOVE(&port->mp_msgq, msg, ms_node);
	msg->ms_flags = (msg->ms_flags & ~MSGF_QUEUED) | MSGF_RETRIEVED;
    } else {
	if (msg->ms_flags & MSGF_RETRIEVED) {
	    /*
	     * abort case, message already returned once, remvoe and
	     * return the aborted message a second time after setting
	     * ms_cmd to ms_abort.
	     */
	    TAILQ_REMOVE(&port->mp_msgq, msg, ms_node);
	    msg->ms_flags &= ~MSGF_QUEUED;
	    msg->ms_cmd = msg->ms_abort;
	} else {
	    /*
	     * abort case, abort races initial message retrieval.  The
	     * message is returned normally but not removed from the 
	     * queue.  On the next loop the 'aborted' message will be
	     * dequeued and returned.  Note that if the caller replies
	     * to the message it will be dequeued (the abort becomes a
	     * NOP).
	     */
	    msg->ms_flags |= MSGF_RETRIEVED;
	}
    }
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

/*
 * This inline helper function completes processing of a reply from an
 * unknown cpu context.
 *
 * The message is being returned to the specified port.  The port is
 * owned by the mp_td thread.  If we are on the same cpu as the mp_td
 * thread we can trivially queue the message to the reply port and schedule
 * the target thread, otherwise we have to send an ipi message to the
 * correct cpu.
 *
 * This inline must be entered with a critical section already held.
 * Note that the IPIQ callback function (*_remote) is entered with a
 * critical section already held, and we obtain one in lwkt_replyport().
 */
static __inline
void
_lwkt_replyport(lwkt_port_t port, lwkt_msg_t msg, int force)
{
    thread_t td = port->mp_td;

    if (force || td->td_gd == mycpu) {
	/*
	 * We can only reply the message if the abort has caught up with us,
	 * or if no abort was issued (same case).
	 */
	if (msg->ms_abort_port == port) {
	    KKASSERT((msg->ms_flags & MSGF_QUEUED) == 0);
	    TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	    msg->ms_flags |= MSGF_DONE | MSGF_QUEUED | MSGF_REPLY2;
	    if (port->mp_flags & MSGPORTF_WAITING)
		lwkt_schedule(td);
	} 
    } else {
	lwkt_send_ipiq(td->td_gd, (ipifunc_t)lwkt_replyport_remote, msg);
    }
}

/*
 * This function completes reply processing for the default case in the
 * context of the originating cpu.
 */
static
void
lwkt_replyport_remote(lwkt_msg_t msg)
{
    _lwkt_replyport(msg->ms_reply_port, msg, 1);
}

/*
 * This function is called in the context of the target to reply a message.
 * The critical section protects us from IPIs on the this CPU.
 */
void
lwkt_default_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    crit_enter();
    msg->ms_flags |= MSGF_REPLY1;

    /*
     * An abort may have caught up to us while we were processing the
     * message.  If this occured we have to dequeue the message from the
     * target port in the context of our current cpu before we can finish
     * replying it.
     */
    if (msg->ms_flags & MSGF_QUEUED) {
	KKASSERT(msg->ms_flags & MSGF_ABORTED);
	TAILQ_REMOVE(&msg->ms_target_port->mp_msgq, msg, ms_node);
	msg->ms_flags &= ~MSGF_QUEUED;
    }

    /*
     * Do reply port processing for async messages.  Just mark the message
     * done and wakeup the owner of the reply port for synchronous messages.
     */
    if (msg->ms_flags & MSGF_ASYNC) {
	_lwkt_replyport(port, msg, 0);
    } else {
	msg->ms_flags |= MSGF_DONE;
	if (port->mp_flags & MSGPORTF_WAITING)
	    lwkt_schedule(port->mp_td);
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
    crit_enter();
    msg->ms_flags |= MSGF_DONE|MSGF_REPLY1;
    crit_exit();
}

/*
 * lwkt_default_putport()
 *
 *	This function is typically assigned to the mp_putport port vector.
 *
 *	Queue a message to the target port and wakeup the thread owning it.
 *	This function always returns EASYNC and may be assigned to a
 *	message port's mp_putport function vector.  Note that we must set
 *	MSGF_QUEUED prior to sending any IPIs in order to interlock against
 *	ABORT requests and other tests that might be performed.
 *
 *	Note that messages start out as synchronous entities, and as an
 *	optimization MSGF_DONE is usually left set (so in the synchronous path
 *	no modifications to ms_flags are ever required).  If a message becomes
 *	async, i.e. you return EASYNC, then MSGF_DONE must be cleared or
 *	lwkt_replymsg() will wind up being a NOP.
 *
 *	The inline must be called from a critical section (the remote function
 *	is called from an IPI and will be in a critical section).
 */
static
__inline
void
_lwkt_putport(lwkt_port_t port, lwkt_msg_t msg, int force)
{
    thread_t td = port->mp_td;

    if (force || td->td_gd == mycpu) {
	TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	if (port->mp_flags & MSGPORTF_WAITING)
	    lwkt_schedule(td);
    } else {
	lwkt_send_ipiq(td->td_gd, (ipifunc_t)lwkt_putport_remote, msg);
    }
}

static
void
lwkt_putport_remote(lwkt_msg_t msg)
{
    _lwkt_putport(msg->ms_target_port, msg, 1);
}

int
lwkt_default_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    crit_enter();
    msg->ms_flags |= MSGF_QUEUED;	/* abort interlock */
    msg->ms_flags &= ~MSGF_DONE;
    msg->ms_target_port = port;
    _lwkt_putport(port, msg, 0);
    crit_exit();
    return(EASYNC);
}

/*
 * lwkt_forwardmsg()
 *
 * Forward a message received on one port to another port.  The forwarding
 * function must deal with a pending abort but othewise essentially just
 * issues a putport to the target port.
 *
 * An abort may have two side effects:  First, the message may have been
 * requeued to the current target port.  If so, we must dequeue it before
 * we can forward it.
 */
int
lwkt_forwardmsg(lwkt_port_t port, lwkt_msg_t msg)
{   
    int error;

    crit_enter();
    if (msg->ms_flags & MSGF_QUEUED) {
	KKASSERT(msg->ms_flags & MSGF_ABORTED);
	TAILQ_REMOVE(&msg->ms_target_port->mp_msgq, msg, ms_node);
	msg->ms_flags &= ~MSGF_QUEUED;
    }
    msg->ms_flags &= ~MSGF_RETRIEVED;
    if ((error = port->mp_putport(port, msg)) != EASYNC)
	lwkt_replymsg(msg, error);
    crit_exit();
    return(error);
}

/*
 * lwkt_abortmsg()
 *
 *	Aborting a message is a fairly complex task.  The first order of
 *	business is to get the message to the cpu that owns the target
 *	port, during which we may have to do some port chasing due to 
 *	message forwarding operations.
 *
 *	NOTE!  Since an aborted message is requeued all message processing
 *	loops should check the MSGF_ABORTED flag.
 */
static void lwkt_abortmsg_remote(lwkt_msg_t msg);

void
lwkt_abortmsg(lwkt_msg_t msg)
{
    lwkt_port_t port;
    thread_t td;

    /*
     * A critical section protects us from reply IPIs on this cpu.   We 
     * can only abort messages that have not yet completed (DONE), are not
     * in the midst of being replied (REPLY1), and which support the
     * abort function (ABORTABLE).
     */
    crit_enter();
    if ((msg->ms_flags & (MSGF_DONE|MSGF_REPLY1|MSGF_ABORTABLE)) == MSGF_ABORTABLE) {
	/*
	 * Chase the message.  If REPLY1 is set the message has been replied
	 * all the way back to the originator, otherwise it is sitting on
	 * ms_target_port (but we can only complete processing if we are
	 * on the same cpu as the selected port in order to avoid
	 * SMP cache synchronization issues).
	 *
	 * When chasing through multiple ports ms_flags may not be 
	 * synchronized to the current cpu, but it WILL be synchronized
	 * with regards to testing the MSGF_REPLY1 bit once we reach the
	 * target port that made the reply and since the cpu owning
	 * some port X stores the new port in ms_target_port if the message
	 * is forwarded, the current port will only ever equal the target
	 * port when we are on the correct cpu.
	 */
	if (msg->ms_flags & MSGF_REPLY1)
	    port = msg->ms_reply_port;
	else
	    port = msg->ms_target_port;
	cpu_mb1();

	/*
	 * The chase call must run on the cpu owning the port.  Fully
	 * synchronous ports (mp_td == NULL) can run the call on any cpu.
	 */
	td = port->mp_td;
	if (td && td->td_gd != mycpu) {
	    lwkt_send_ipiq(td->td_gd, (ipifunc_t)lwkt_abortmsg_remote, msg);
	} else {
	    port->mp_abortport(port, msg);
	}
    }
    crit_exit();
}

static
void
lwkt_abortmsg_remote(lwkt_msg_t msg)
{
    lwkt_port_t port;
    thread_t td;

    if (msg->ms_flags & MSGF_REPLY1)
	port = msg->ms_reply_port;
    else
	port = msg->ms_target_port;
    cpu_mb1();
    td = port->mp_td;
    if (td->td_gd != mycpu) {
	lwkt_send_ipiq(td->td_gd, (ipifunc_t)lwkt_abortmsg_remote, msg);
    } else {
	port->mp_abortport(port, msg);
    }
}

/*
 * The mp_abortport function is called when the abort has finally caught up
 * to the target port or (if the message has been replied) the reply port.
 */
void
lwkt_default_abortport(lwkt_port_t port, lwkt_msg_t msg)
{
    /*
     * Set ms_abort_port to ms_reply_port to indicate the completion of
     * the messaging chasing portion of the abort request.  Note that
     * the passed port is the port that we finally caught up to, not
     * necessarily the reply port.
     */
    msg->ms_abort_port = msg->ms_reply_port;

    if (msg->ms_flags & MSGF_REPLY2) {
	/*
	 * If REPLY2 is set we must have chased it all the way back to
	 * the reply port, but the replyport code has not queued the message
	 * (because it was waiting for the abort to catch up).  We become
	 * responsible for queueing the message to the reply port.
	 */
	KKASSERT((msg->ms_flags & MSGF_QUEUED) == 0);
	KKASSERT(port == msg->ms_reply_port);
	TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	msg->ms_flags |= MSGF_DONE | MSGF_QUEUED;
	if (port->mp_flags & MSGPORTF_WAITING)
	    lwkt_schedule(port->mp_td);
    } else if ((msg->ms_flags & (MSGF_QUEUED|MSGF_REPLY1)) == 0) {
	/*
	 * Abort on the target port.  The message has not yet been replied
	 * and must be requeued to the target port.
	 */
	msg->ms_flags |= MSGF_ABORTED | MSGF_QUEUED;
	TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	if (port->mp_flags & MSGPORTF_WAITING)
	    lwkt_schedule(port->mp_td);
    } else if ((msg->ms_flags & MSGF_REPLY1) == 0) {
	/*
	 * The message has not yet been retrieved by the target port, set
	 * MSGF_ABORTED so the target port can requeue the message abort after
	 * retrieving it.
	 */
	msg->ms_flags |= MSGF_ABORTED;
    }
}

/*
 * lwkt_default_waitport()
 *
 *	If msg is NULL, dequeue the next message from the port's message
 *	queue, block until a message is ready.  This function never
 *	returns NULL.
 *
 *	If msg is non-NULL, block until the requested message has been returned
 *	to the port then dequeue and return it.  DO NOT USE THIS TO WAIT FOR
 *	INCOMING REQUESTS, ONLY USE THIS TO WAIT FOR REPLIES.
 *
 *	Note that the API does not currently support multiple threads waiting
 * 	on a port.  By virtue of owning the port it is controlled by our
 *	cpu and we can safely manipulate it's contents.
 */
void *
lwkt_default_waitport(lwkt_port_t port, lwkt_msg_t msg)
{
    thread_t td = curthread;
    int sentabort;

    KKASSERT(port->mp_td == td);
    crit_enter_quick(td);
    if (msg == NULL) {
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
	 * If a message is not marked done, or if it is queued, we have work
	 * to do.  Note that MSGF_DONE is always set in the context of the
	 * reply port's cpu.
	 */
	if ((msg->ms_flags & (MSGF_DONE|MSGF_QUEUED)) != MSGF_DONE) {
	    /*
	     * We must own the reply port to safely mess with it's contents.
	     */
	    port = msg->ms_reply_port;
	    KKASSERT(port->mp_td == td);

	    if ((msg->ms_flags & MSGF_DONE) == 0) {
		port->mp_flags |= MSGPORTF_WAITING; /* saved by the BGL */
		sentabort = 0;
		do {
#ifdef _KERNEL
		    /*
		     * MSGF_PCATCH is only set by processes which wish to
		     * abort the message they are blocked on when a signal
		     * occurs.  Note that we still must wait for message
		     * completion after sending an abort request.
		     */
		    if (msg->ms_flags & MSGF_PCATCH) {
			if (sentabort == 0 && CURSIG(port->mp_td->td_proc)) {
			    sentabort = 1;
			    lwkt_abortmsg(msg);
			    continue;
			}
		    }
#endif
		    /*
		     * XXX set TDF_SINTR so 'ps' knows the difference between
		     * an interruptable wait and a disk wait.  YYY eventually
		     * move P_SINTR to TDF_SINTR to reduce duplication.
		     */
		    td->td_flags |= TDF_SINTR | TDF_BLOCKED;
		    lwkt_deschedule_self(td);
		    lwkt_switch();
		    td->td_flags &= ~(TDF_SINTR | TDF_BLOCKED);
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

