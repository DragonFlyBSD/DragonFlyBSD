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
 */

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
#include <sys/spinlock2.h>
#include <sys/serialize.h>

#include <machine/stdarg.h>
#include <machine/cpufunc.h>
#include <machine/smp.h>

#include <sys/malloc.h>
MALLOC_DEFINE(M_LWKTMSG, "lwkt message", "lwkt message");

/************************************************************************
 *				MESSAGE FUNCTIONS			*
 ************************************************************************/

static __inline void
_lwkt_sendmsg_prepare(lwkt_port_t port, lwkt_msg_t msg)
{
    KKASSERT(msg->ms_reply_port != NULL &&
	     (msg->ms_flags & (MSGF_DONE|MSGF_QUEUED)) == MSGF_DONE);
    msg->ms_flags &= ~(MSGF_REPLY | MSGF_SYNC | MSGF_DONE);
}

static __inline void
_lwkt_sendmsg_start(lwkt_port_t port, lwkt_msg_t msg)
{
    int error;

    if ((error = lwkt_beginmsg(port, msg)) != EASYNC) {
	/*
	 * Target port opted to execute the message synchronously so
	 * queue the response.
	 */
	lwkt_replymsg(msg, error);
    }
}

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
 *
 *	NOTE: Do not use this function to forward a message as we might
 *	clobber ms_flags in a SMP race.
 */
void
lwkt_sendmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    _lwkt_sendmsg_prepare(port, msg);
    _lwkt_sendmsg_start(port, msg);
}

void
lwkt_sendmsg_prepare(lwkt_port_t port, lwkt_msg_t msg)
{
    _lwkt_sendmsg_prepare(port, msg);
}

void
lwkt_sendmsg_start(lwkt_port_t port, lwkt_msg_t msg)
{
    _lwkt_sendmsg_start(port, msg);
}

/*
 * lwkt_domsg()
 *
 *	Request synchronous completion and call lwkt_beginmsg().  The
 *	target port can opt to execute the message synchronously or
 *	asynchronously and this function will automatically block and
 *	wait for a response if the target executes the message
 *	asynchronously.
 *
 *	NOTE: Do not use this function to forward a message as we might
 *	clobber ms_flags in a SMP race.
 */
int
lwkt_domsg(lwkt_port_t port, lwkt_msg_t msg, int flags)
{
    int error;

    KKASSERT(msg->ms_reply_port != NULL &&
	     (msg->ms_flags & (MSGF_DONE|MSGF_QUEUED)) == MSGF_DONE);
    msg->ms_flags &= ~(MSGF_REPLY | MSGF_DONE);
    msg->ms_flags |= MSGF_SYNC;
    if ((error = lwkt_beginmsg(port, msg)) == EASYNC) {
	/*
	 * Target port opted to execute the message asynchronously so
	 * block and wait for a reply.
	 */
	error = lwkt_waitmsg(msg, flags);
    } else {
	msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
    }
    return(error);
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

    KKASSERT((msg->ms_flags & (MSGF_QUEUED|MSGF_DONE|MSGF_REPLY)) == 0);
    if ((error = port->mp_putport(port, msg)) != EASYNC)
	lwkt_replymsg(msg, error);
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

/************************************************************************
 *			PORT INITIALIZATION API				*
 ************************************************************************/

static void *lwkt_thread_getport(lwkt_port_t port);
static int lwkt_thread_putport(lwkt_port_t port, lwkt_msg_t msg);
static int lwkt_thread_waitmsg(lwkt_msg_t msg, int flags);
static void *lwkt_thread_waitport(lwkt_port_t port, int flags);
static void lwkt_thread_replyport(lwkt_port_t port, lwkt_msg_t msg);
static int lwkt_thread_dropmsg(lwkt_port_t port, lwkt_msg_t msg);

static void *lwkt_spin_getport(lwkt_port_t port);
static int lwkt_spin_putport(lwkt_port_t port, lwkt_msg_t msg);
static int lwkt_spin_waitmsg(lwkt_msg_t msg, int flags);
static void *lwkt_spin_waitport(lwkt_port_t port, int flags);
static void lwkt_spin_replyport(lwkt_port_t port, lwkt_msg_t msg);
static int lwkt_spin_dropmsg(lwkt_port_t port, lwkt_msg_t msg);

static void *lwkt_serialize_getport(lwkt_port_t port);
static int lwkt_serialize_putport(lwkt_port_t port, lwkt_msg_t msg);
static int lwkt_serialize_waitmsg(lwkt_msg_t msg, int flags);
static void *lwkt_serialize_waitport(lwkt_port_t port, int flags);
static void lwkt_serialize_replyport(lwkt_port_t port, lwkt_msg_t msg);

static void lwkt_null_replyport(lwkt_port_t port, lwkt_msg_t msg);
static void *lwkt_panic_getport(lwkt_port_t port);
static int lwkt_panic_putport(lwkt_port_t port, lwkt_msg_t msg);
static int lwkt_panic_waitmsg(lwkt_msg_t msg, int flags);
static void *lwkt_panic_waitport(lwkt_port_t port, int flags);
static void lwkt_panic_replyport(lwkt_port_t port, lwkt_msg_t msg);
static int lwkt_panic_dropmsg(lwkt_port_t port, lwkt_msg_t msg);

/*
 * Core port initialization (internal)
 */
static __inline
void
_lwkt_initport(lwkt_port_t port,
	       void *(*gportfn)(lwkt_port_t),
	       int (*pportfn)(lwkt_port_t, lwkt_msg_t),
	       int (*wmsgfn)(lwkt_msg_t, int),
	       void *(*wportfn)(lwkt_port_t, int),
	       void (*rportfn)(lwkt_port_t, lwkt_msg_t),
	       int (*dmsgfn)(lwkt_port_t, lwkt_msg_t))
{
    bzero(port, sizeof(*port));
    TAILQ_INIT(&port->mp_msgq);
    TAILQ_INIT(&port->mp_msgq_prio);
    port->mp_getport = gportfn;
    port->mp_putport = pportfn;
    port->mp_waitmsg =  wmsgfn;
    port->mp_waitport =  wportfn;
    port->mp_replyport = rportfn;
    port->mp_dropmsg = dmsgfn;
}

/*
 * Schedule the target thread.  If the message flags contains MSGF_NORESCHED
 * we tell the scheduler not to reschedule if td is at a higher priority.
 *
 * This routine is called even if the thread is already scheduled.
 */
static __inline
void
_lwkt_schedule_msg(thread_t td, int flags)
{
    lwkt_schedule(td);
}

/*
 * lwkt_initport_thread()
 *
 *	Initialize a port for use by a particular thread.  The port may
 *	only be used by <td>.
 */
void
lwkt_initport_thread(lwkt_port_t port, thread_t td)
{
    _lwkt_initport(port,
		   lwkt_thread_getport,
		   lwkt_thread_putport,
		   lwkt_thread_waitmsg,
		   lwkt_thread_waitport,
		   lwkt_thread_replyport,
		   lwkt_thread_dropmsg);
    port->mpu_td = td;
}

/*
 * lwkt_initport_spin()
 *
 *	Initialize a port for use with descriptors that might be accessed
 *	via multiple LWPs, processes, or threads.  Has somewhat more
 *	overhead then thread ports.
 */
void
lwkt_initport_spin(lwkt_port_t port, thread_t td)
{
    int (*dmsgfn)(lwkt_port_t, lwkt_msg_t);

    if (td == NULL)
	dmsgfn = lwkt_panic_dropmsg;
    else
	dmsgfn = lwkt_spin_dropmsg;

    _lwkt_initport(port,
		   lwkt_spin_getport,
		   lwkt_spin_putport,
		   lwkt_spin_waitmsg,
		   lwkt_spin_waitport,
		   lwkt_spin_replyport,
		   dmsgfn);
    spin_init(&port->mpu_spin);
    port->mpu_td = td;
}

/*
 * lwkt_initport_serialize()
 *
 *	Initialize a port for use with descriptors that might be accessed
 *	via multiple LWPs, processes, or threads.  Callers are assumed to
 *	have held the serializer (slz).
 */
void
lwkt_initport_serialize(lwkt_port_t port, struct lwkt_serialize *slz)
{
    _lwkt_initport(port,
		   lwkt_serialize_getport,
		   lwkt_serialize_putport,
		   lwkt_serialize_waitmsg,
		   lwkt_serialize_waitport,
		   lwkt_serialize_replyport,
		   lwkt_panic_dropmsg);
    port->mpu_serialize = slz;
}

/*
 * Similar to the standard initport, this function simply marks the message
 * as being done and does not attempt to return it to an originating port.
 */
void
lwkt_initport_replyonly_null(lwkt_port_t port)
{
    _lwkt_initport(port,
		   lwkt_panic_getport,
		   lwkt_panic_putport,
		   lwkt_panic_waitmsg,
		   lwkt_panic_waitport,
		   lwkt_null_replyport,
		   lwkt_panic_dropmsg);
}

/*
 * Initialize a reply-only port, typically used as a message sink.  Such
 * ports can only be used as a reply port.
 */
void
lwkt_initport_replyonly(lwkt_port_t port,
			void (*rportfn)(lwkt_port_t, lwkt_msg_t))
{
    _lwkt_initport(port, lwkt_panic_getport, lwkt_panic_putport,
			 lwkt_panic_waitmsg, lwkt_panic_waitport,
			 rportfn, lwkt_panic_dropmsg);
}

void
lwkt_initport_putonly(lwkt_port_t port,
		      int (*pportfn)(lwkt_port_t, lwkt_msg_t))
{
    _lwkt_initport(port, lwkt_panic_getport, pportfn,
			 lwkt_panic_waitmsg, lwkt_panic_waitport,
			 lwkt_panic_replyport, lwkt_panic_dropmsg);
}

void
lwkt_initport_panic(lwkt_port_t port)
{
    _lwkt_initport(port,
		   lwkt_panic_getport, lwkt_panic_putport,
		   lwkt_panic_waitmsg, lwkt_panic_waitport,
		   lwkt_panic_replyport, lwkt_panic_dropmsg);
}

static __inline
void
_lwkt_pullmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    lwkt_msg_queue *queue;

    /*
     * normal case, remove and return the message.
     */
    if (__predict_false(msg->ms_flags & MSGF_PRIORITY))
	queue = &port->mp_msgq_prio;
    else
	queue = &port->mp_msgq;
    TAILQ_REMOVE(queue, msg, ms_node);

    /*
     * atomic op needed for spin ports
     */
    atomic_clear_int(&msg->ms_flags, MSGF_QUEUED);
}

static __inline
void
_lwkt_pushmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    lwkt_msg_queue *queue;

    /*
     * atomic op needed for spin ports
     */
    atomic_set_int(&msg->ms_flags, MSGF_QUEUED);
    if (__predict_false(msg->ms_flags & MSGF_PRIORITY))
	queue = &port->mp_msgq_prio;
    else
    	queue = &port->mp_msgq;
    TAILQ_INSERT_TAIL(queue, msg, ms_node);
}

static __inline
lwkt_msg_t
_lwkt_pollmsg(lwkt_port_t port)
{
    lwkt_msg_t msg;

    msg = TAILQ_FIRST(&port->mp_msgq_prio);
    if (__predict_false(msg != NULL))
	return msg;

    /*
     * Priority queue has no message, fallback to non-priority queue.
     */
    return TAILQ_FIRST(&port->mp_msgq);
}

static __inline
void
_lwkt_enqueue_reply(lwkt_port_t port, lwkt_msg_t msg)
{
    /*
     * atomic op needed for spin ports
     */
    _lwkt_pushmsg(port, msg);
    atomic_set_int(&msg->ms_flags, MSGF_REPLY | MSGF_DONE);
}

/************************************************************************
 *			THREAD PORT BACKEND				*
 ************************************************************************
 *
 * This backend is used when the port a message is retrieved from is owned
 * by a single thread (the calling thread).  Messages are IPId to the
 * correct cpu before being enqueued to a port.  Note that this is fairly
 * optimal since scheduling would have had to do an IPI anyway if the
 * message were headed to a different cpu.
 */

/*
 * This function completes reply processing for the default case in the
 * context of the originating cpu.
 */
static
void
lwkt_thread_replyport_remote(lwkt_msg_t msg)
{
    lwkt_port_t port = msg->ms_reply_port;
    int flags;

    /*
     * Chase any thread migration that occurs
     */
    if (port->mpu_td->td_gd != mycpu) {
	lwkt_send_ipiq(port->mpu_td->td_gd,
		       (ipifunc1_t)lwkt_thread_replyport_remote, msg);
	return;
    }

    /*
     * Cleanup (in critical section, IPI on same cpu, atomic op not needed)
     */
#ifdef INVARIANTS
    KKASSERT(msg->ms_flags & MSGF_INTRANSIT);
    msg->ms_flags &= ~MSGF_INTRANSIT;
#endif
    flags = msg->ms_flags;
    if (msg->ms_flags & MSGF_SYNC) {
	cpu_sfence();
	msg->ms_flags |= MSGF_REPLY | MSGF_DONE;
    } else {
	_lwkt_enqueue_reply(port, msg);
    }
    if (port->mp_flags & MSGPORTF_WAITING)
	_lwkt_schedule_msg(port->mpu_td, flags);
}

/*
 * lwkt_thread_replyport() - Backend to lwkt_replymsg()
 *
 * Called with the reply port as an argument but in the context of the
 * original target port.  Completion must occur on the target port's
 * cpu.
 *
 * The critical section protects us from IPIs on the this CPU.
 */
static
void
lwkt_thread_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    int flags;

    KKASSERT((msg->ms_flags & (MSGF_DONE|MSGF_QUEUED|MSGF_INTRANSIT)) == 0);

    if (msg->ms_flags & MSGF_SYNC) {
	/*
	 * If a synchronous completion has been requested, just wakeup
	 * the message without bothering to queue it to the target port.
	 *
	 * Assume the target thread is non-preemptive, so no critical
	 * section is required.
	 */
	if (port->mpu_td->td_gd == mycpu) {
	    crit_enter();
	    flags = msg->ms_flags;
	    cpu_sfence();
	    msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
	    if (port->mp_flags & MSGPORTF_WAITING)
		_lwkt_schedule_msg(port->mpu_td, flags);
	    crit_exit();
	} else {
#ifdef INVARIANTS
	    atomic_set_int(&msg->ms_flags, MSGF_INTRANSIT);
#endif
	    atomic_set_int(&msg->ms_flags, MSGF_REPLY);
	    lwkt_send_ipiq(port->mpu_td->td_gd,
			   (ipifunc1_t)lwkt_thread_replyport_remote, msg);
	}
    } else {
	/*
	 * If an asynchronous completion has been requested the message
	 * must be queued to the reply port.
	 *
	 * A critical section is required to interlock the port queue.
	 */
	if (port->mpu_td->td_gd == mycpu) {
	    crit_enter();
	    _lwkt_enqueue_reply(port, msg);
	    if (port->mp_flags & MSGPORTF_WAITING)
		_lwkt_schedule_msg(port->mpu_td, msg->ms_flags);
	    crit_exit();
	} else {
#ifdef INVARIANTS
	    atomic_set_int(&msg->ms_flags, MSGF_INTRANSIT);
#endif
	    atomic_set_int(&msg->ms_flags, MSGF_REPLY);
	    lwkt_send_ipiq(port->mpu_td->td_gd,
			   (ipifunc1_t)lwkt_thread_replyport_remote, msg);
	}
    }
}

/*
 * lwkt_thread_dropmsg() - Backend to lwkt_dropmsg()
 *
 * This function could _only_ be used when caller is in the same thread
 * as the message's target port owner thread.
 */
static int
lwkt_thread_dropmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    int error;

    KASSERT(port->mpu_td == curthread,
    	    ("message could only be dropped in the same thread "
	     "as the message target port thread"));
    crit_enter_quick(port->mpu_td);
    if ((msg->ms_flags & (MSGF_REPLY|MSGF_QUEUED)) == MSGF_QUEUED) {
	    _lwkt_pullmsg(port, msg);
	    atomic_set_int(&msg->ms_flags, MSGF_DONE);
	    error = 0;
    } else {
	    error = ENOENT;
    }
    crit_exit_quick(port->mpu_td);

    return (error);
}

/*
 * lwkt_thread_putport() - Backend to lwkt_beginmsg()
 *
 * Called with the target port as an argument but in the context of the
 * reply port.  This function always implements an asynchronous put to
 * the target message port, and thus returns EASYNC.
 *
 * The message must already have cleared MSGF_DONE and MSGF_REPLY
 */
static
void
lwkt_thread_putport_remote(lwkt_msg_t msg)
{
    lwkt_port_t port = msg->ms_target_port;

    /*
     * Chase any thread migration that occurs
     */
    if (port->mpu_td->td_gd != mycpu) {
	lwkt_send_ipiq(port->mpu_td->td_gd,
		       (ipifunc1_t)lwkt_thread_putport_remote, msg);
	return;
    }

    /*
     * An atomic op is needed on ms_flags vs originator.  Also
     * note that the originator might be using a different type
     * of msgport.
     */
#ifdef INVARIANTS
    KKASSERT(msg->ms_flags & MSGF_INTRANSIT);
    atomic_clear_int(&msg->ms_flags, MSGF_INTRANSIT);
#endif
    _lwkt_pushmsg(port, msg);
    if (port->mp_flags & MSGPORTF_WAITING)
	_lwkt_schedule_msg(port->mpu_td, msg->ms_flags);
}

static
int
lwkt_thread_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    KKASSERT((msg->ms_flags & (MSGF_DONE | MSGF_REPLY)) == 0);

    msg->ms_target_port = port;
    if (port->mpu_td->td_gd == mycpu) {
	crit_enter();
	_lwkt_pushmsg(port, msg);
	if (port->mp_flags & MSGPORTF_WAITING)
	    _lwkt_schedule_msg(port->mpu_td, msg->ms_flags);
	crit_exit();
    } else {
#ifdef INVARIANTS
	/*
	 * Cleanup.
	 *
	 * An atomic op is needed on ms_flags vs originator.  Also
	 * note that the originator might be using a different type
	 * of msgport.
	 */
	atomic_set_int(&msg->ms_flags, MSGF_INTRANSIT);
#endif
	lwkt_send_ipiq(port->mpu_td->td_gd,
			(ipifunc1_t)lwkt_thread_putport_remote, msg);
    }
    return (EASYNC);
}

/*
 * lwkt_thread_getport()
 *
 *	Retrieve the next message from the port or NULL if no messages
 *	are ready.
 */
static
void *
lwkt_thread_getport(lwkt_port_t port)
{
    lwkt_msg_t msg;

    KKASSERT(port->mpu_td == curthread);

    crit_enter_quick(port->mpu_td);
    if ((msg = _lwkt_pollmsg(port)) != NULL)
	_lwkt_pullmsg(port, msg);
    crit_exit_quick(port->mpu_td);
    return(msg);
}

/*
 * lwkt_thread_waitmsg()
 *
 *	Wait for a particular message to be replied.  We must be the only
 *	thread waiting on the message.  The port must be owned by the
 *	caller.
 */
static
int
lwkt_thread_waitmsg(lwkt_msg_t msg, int flags)
{
    thread_t td = curthread;

    KASSERT((msg->ms_flags & MSGF_DROPABLE) == 0,
	    ("can't wait dropable message"));

    if ((msg->ms_flags & MSGF_DONE) == 0) {
	/*
	 * If the done bit was not set we have to block until it is.
	 */
	lwkt_port_t port = msg->ms_reply_port;
	int sentabort;

	KKASSERT(port->mpu_td == td);
	crit_enter_quick(td);
	sentabort = 0;

	while ((msg->ms_flags & MSGF_DONE) == 0) {
	    port->mp_flags |= MSGPORTF_WAITING;	/* same cpu */
	    if (sentabort == 0) {
		if ((sentabort = lwkt_sleep("waitmsg", flags)) != 0) {
		    lwkt_abortmsg(msg);
		}
	    } else {
		lwkt_sleep("waitabt", 0);
	    }
	    port->mp_flags &= ~MSGPORTF_WAITING;
	}
	if (msg->ms_flags & MSGF_QUEUED)
	    _lwkt_pullmsg(port, msg);
	crit_exit_quick(td);
    } else {
	/*
	 * If the done bit was set we only have to mess around with the
	 * message if it is queued on the reply port.
	 */
	crit_enter_quick(td);
	if (msg->ms_flags & MSGF_QUEUED) {
	    lwkt_port_t port = msg->ms_reply_port;
	    thread_t td __debugvar = curthread;

	    KKASSERT(port->mpu_td == td);
	    _lwkt_pullmsg(port, msg);
	}
	crit_exit_quick(td);
    }
    return(msg->ms_error);
}

/*
 * lwkt_thread_waitport()
 *
 *	Wait for a new message to be available on the port.  We must be the
 *	the only thread waiting on the port.  The port must be owned by caller.
 */
static
void *
lwkt_thread_waitport(lwkt_port_t port, int flags)
{
    thread_t td = curthread;
    lwkt_msg_t msg;
    int error;

    KKASSERT(port->mpu_td == td);
    crit_enter_quick(td);
    while ((msg = _lwkt_pollmsg(port)) == NULL) {
	port->mp_flags |= MSGPORTF_WAITING;
	error = lwkt_sleep("waitport", flags);
	port->mp_flags &= ~MSGPORTF_WAITING;
	if (error)
	    goto done;
    }
    _lwkt_pullmsg(port, msg);
done:
    crit_exit_quick(td);
    return(msg);
}

/************************************************************************
 *			   SPIN PORT BACKEND				*
 ************************************************************************
 *
 * This backend uses spinlocks instead of making assumptions about which
 * thread is accessing the port.  It must be used when a port is not owned
 * by a particular thread.  This is less optimal then thread ports but
 * you don't have a choice if there are multiple threads accessing the port.
 *
 * Note on MSGPORTF_WAITING - because there may be multiple threads blocked
 * on the message port, it is the responsibility of the code doing the
 * wakeup to clear this flag rather then the blocked threads.  Some
 * superfluous wakeups may occur, which is ok.
 *
 * XXX synchronous message wakeups are not current optimized.
 */

static
void *
lwkt_spin_getport(lwkt_port_t port)
{
    lwkt_msg_t msg;

    spin_lock(&port->mpu_spin);
    if ((msg = _lwkt_pollmsg(port)) != NULL)
	_lwkt_pullmsg(port, msg);
    spin_unlock(&port->mpu_spin);
    return(msg);
}

static
int
lwkt_spin_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    int dowakeup;

    KKASSERT((msg->ms_flags & (MSGF_DONE | MSGF_REPLY)) == 0);

    msg->ms_target_port = port;
    spin_lock(&port->mpu_spin);
    _lwkt_pushmsg(port, msg);
    dowakeup = 0;
    if (port->mp_flags & MSGPORTF_WAITING) {
	port->mp_flags &= ~MSGPORTF_WAITING;
	dowakeup = 1;
    }
    spin_unlock(&port->mpu_spin);
    if (dowakeup)
	wakeup(port);
    return (EASYNC);
}

static
int
lwkt_spin_waitmsg(lwkt_msg_t msg, int flags)
{
    lwkt_port_t port;
    int sentabort;
    int error;

    KASSERT((msg->ms_flags & MSGF_DROPABLE) == 0,
	    ("can't wait dropable message"));
    port = msg->ms_reply_port;

    if ((msg->ms_flags & MSGF_DONE) == 0) {
	sentabort = 0;
	spin_lock(&port->mpu_spin);
	while ((msg->ms_flags & MSGF_DONE) == 0) {
	    void *won;

	    /*
	     * If message was sent synchronously from the beginning
	     * the wakeup will be on the message structure, else it
	     * will be on the port structure.
	     *
	     * ms_flags needs atomic op originator vs target MSGF_QUEUED
	     */
	    if (msg->ms_flags & MSGF_SYNC) {
		won = msg;
		atomic_set_int(&msg->ms_flags, MSGF_WAITING);
	    } else {
		won = port;
		port->mp_flags |= MSGPORTF_WAITING;
	    }

	    /*
	     * Only messages which support abort can be interrupted.
	     * We must still wait for message completion regardless.
	     */
	    if ((flags & PCATCH) && sentabort == 0) {
		error = ssleep(won, &port->mpu_spin, PCATCH, "waitmsg", 0);
		if (error) {
		    sentabort = error;
		    spin_unlock(&port->mpu_spin);
		    lwkt_abortmsg(msg);
		    spin_lock(&port->mpu_spin);
		}
	    } else {
		error = ssleep(won, &port->mpu_spin, 0, "waitmsg", 0);
	    }
	    /* see note at the top on the MSGPORTF_WAITING flag */
	}
	/*
	 * Turn EINTR into ERESTART if the signal indicates.
	 */
	if (sentabort && msg->ms_error == EINTR)
	    msg->ms_error = sentabort;
	if (msg->ms_flags & MSGF_QUEUED)
	    _lwkt_pullmsg(port, msg);
	spin_unlock(&port->mpu_spin);
    } else {
	spin_lock(&port->mpu_spin);
	if (msg->ms_flags & MSGF_QUEUED) {
	    _lwkt_pullmsg(port, msg);
	}
	spin_unlock(&port->mpu_spin);
    }
    return(msg->ms_error);
}

static
void *
lwkt_spin_waitport(lwkt_port_t port, int flags)
{
    lwkt_msg_t msg;
    int error;

    spin_lock(&port->mpu_spin);
    while ((msg = _lwkt_pollmsg(port)) == NULL) {
	port->mp_flags |= MSGPORTF_WAITING;
	error = ssleep(port, &port->mpu_spin, flags, "waitport", 0);
	/* see note at the top on the MSGPORTF_WAITING flag */
	if (error) {
	    spin_unlock(&port->mpu_spin);
	    return(NULL);
	}
    }
    _lwkt_pullmsg(port, msg);
    spin_unlock(&port->mpu_spin);
    return(msg);
}

static
void
lwkt_spin_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    int dowakeup;

    KKASSERT((msg->ms_flags & (MSGF_DONE|MSGF_QUEUED)) == 0);

    if (msg->ms_flags & MSGF_SYNC) {
	/*
	 * If a synchronous completion has been requested, just wakeup
	 * the message without bothering to queue it to the target port.
	 *
	 * ms_flags protected by reply port spinlock
	 */
	spin_lock(&port->mpu_spin);
	msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
	dowakeup = 0;
	if (msg->ms_flags & MSGF_WAITING) {
		msg->ms_flags &= ~MSGF_WAITING;
		dowakeup = 1;
	}
	spin_unlock(&port->mpu_spin);
	if (dowakeup)
		wakeup(msg);
    } else {
	/*
	 * If an asynchronous completion has been requested the message
	 * must be queued to the reply port.
	 */
	spin_lock(&port->mpu_spin);
	_lwkt_enqueue_reply(port, msg);
	dowakeup = 0;
	if (port->mp_flags & MSGPORTF_WAITING) {
	    port->mp_flags &= ~MSGPORTF_WAITING;
	    dowakeup = 1;
	}
	spin_unlock(&port->mpu_spin);
	if (dowakeup)
	    wakeup(port);
    }
}

/*
 * lwkt_spin_dropmsg() - Backend to lwkt_dropmsg()
 *
 * This function could _only_ be used when caller is in the same thread
 * as the message's target port owner thread.
 */
static int
lwkt_spin_dropmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    int error;

    KASSERT(port->mpu_td == curthread,
    	    ("message could only be dropped in the same thread "
	     "as the message target port thread\n"));
    spin_lock(&port->mpu_spin);
    if ((msg->ms_flags & (MSGF_REPLY|MSGF_QUEUED)) == MSGF_QUEUED) {
	    _lwkt_pullmsg(port, msg);
	    msg->ms_flags |= MSGF_DONE;
	    error = 0;
    } else {
	    error = ENOENT;
    }
    spin_unlock(&port->mpu_spin);

    return (error);
}

/************************************************************************
 *			  SERIALIZER PORT BACKEND			*
 ************************************************************************
 *
 * This backend uses serializer to protect port accessing.  Callers are
 * assumed to have serializer held.  This kind of port is usually created
 * by network device driver along with _one_ lwkt thread to pipeline
 * operations which may temporarily release serializer.
 *
 * Implementation is based on SPIN PORT BACKEND.
 */

static
void *
lwkt_serialize_getport(lwkt_port_t port)
{
    lwkt_msg_t msg;

    ASSERT_SERIALIZED(port->mpu_serialize);

    if ((msg = _lwkt_pollmsg(port)) != NULL)
	_lwkt_pullmsg(port, msg);
    return(msg);
}

static
int
lwkt_serialize_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    KKASSERT((msg->ms_flags & (MSGF_DONE | MSGF_REPLY)) == 0);
    ASSERT_SERIALIZED(port->mpu_serialize);

    msg->ms_target_port = port;
    _lwkt_pushmsg(port, msg);
    if (port->mp_flags & MSGPORTF_WAITING) {
	port->mp_flags &= ~MSGPORTF_WAITING;
	wakeup(port);
    }
    return (EASYNC);
}

static
int
lwkt_serialize_waitmsg(lwkt_msg_t msg, int flags)
{
    lwkt_port_t port;
    int sentabort;
    int error;

    KASSERT((msg->ms_flags & MSGF_DROPABLE) == 0,
	    ("can't wait dropable message"));

    if ((msg->ms_flags & MSGF_DONE) == 0) {
	port = msg->ms_reply_port;

	ASSERT_SERIALIZED(port->mpu_serialize);

	sentabort = 0;
	while ((msg->ms_flags & MSGF_DONE) == 0) {
	    void *won;

	    /*
	     * If message was sent synchronously from the beginning
	     * the wakeup will be on the message structure, else it
	     * will be on the port structure.
	     */
	    if (msg->ms_flags & MSGF_SYNC) {
		won = msg;
	    } else {
		won = port;
		port->mp_flags |= MSGPORTF_WAITING;
	    }

	    /*
	     * Only messages which support abort can be interrupted.
	     * We must still wait for message completion regardless.
	     */
	    if ((flags & PCATCH) && sentabort == 0) {
		error = zsleep(won, port->mpu_serialize, PCATCH, "waitmsg", 0);
		if (error) {
		    sentabort = error;
		    lwkt_serialize_exit(port->mpu_serialize);
		    lwkt_abortmsg(msg);
		    lwkt_serialize_enter(port->mpu_serialize);
		}
	    } else {
		error = zsleep(won, port->mpu_serialize, 0, "waitmsg", 0);
	    }
	    /* see note at the top on the MSGPORTF_WAITING flag */
	}
	/*
	 * Turn EINTR into ERESTART if the signal indicates.
	 */
	if (sentabort && msg->ms_error == EINTR)
	    msg->ms_error = sentabort;
	if (msg->ms_flags & MSGF_QUEUED)
	    _lwkt_pullmsg(port, msg);
    } else {
	if (msg->ms_flags & MSGF_QUEUED) {
	    port = msg->ms_reply_port;

	    ASSERT_SERIALIZED(port->mpu_serialize);
	    _lwkt_pullmsg(port, msg);
	}
    }
    return(msg->ms_error);
}

static
void *
lwkt_serialize_waitport(lwkt_port_t port, int flags)
{
    lwkt_msg_t msg;
    int error;

    ASSERT_SERIALIZED(port->mpu_serialize);

    while ((msg = _lwkt_pollmsg(port)) == NULL) {
	port->mp_flags |= MSGPORTF_WAITING;
	error = zsleep(port, port->mpu_serialize, flags, "waitport", 0);
	/* see note at the top on the MSGPORTF_WAITING flag */
	if (error)
	    return(NULL);
    }
    _lwkt_pullmsg(port, msg);
    return(msg);
}

static
void
lwkt_serialize_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    KKASSERT((msg->ms_flags & (MSGF_DONE|MSGF_QUEUED)) == 0);
    ASSERT_SERIALIZED(port->mpu_serialize);

    if (msg->ms_flags & MSGF_SYNC) {
	/*
	 * If a synchronous completion has been requested, just wakeup
	 * the message without bothering to queue it to the target port.
	 *
	 * (both sides synchronized via serialized reply port)
	 */
	msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
	wakeup(msg);
    } else {
	/*
	 * If an asynchronous completion has been requested the message
	 * must be queued to the reply port.
	 */
	_lwkt_enqueue_reply(port, msg);
	if (port->mp_flags & MSGPORTF_WAITING) {
	    port->mp_flags &= ~MSGPORTF_WAITING;
	    wakeup(port);
	}
    }
}

/************************************************************************
 *		     PANIC AND SPECIAL PORT FUNCTIONS			*
 ************************************************************************/

/*
 * You can point a port's reply vector at this function if you just want
 * the message marked done, without any queueing or signaling.  This is
 * often used for structure-embedded messages.
 */
static
void
lwkt_null_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
}

static
void *
lwkt_panic_getport(lwkt_port_t port)
{
    panic("lwkt_getport() illegal on port %p", port);
}

static
int
lwkt_panic_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    panic("lwkt_begin/do/sendmsg() illegal on port %p msg %p", port, msg);
}

static
int
lwkt_panic_waitmsg(lwkt_msg_t msg, int flags)
{
    panic("port %p msg %p cannot be waited on", msg->ms_reply_port, msg);
}

static
void *
lwkt_panic_waitport(lwkt_port_t port, int flags)
{
    panic("port %p cannot be waited on", port);
}

static
void
lwkt_panic_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    panic("lwkt_replymsg() is illegal on port %p msg %p", port, msg);
}

static
int
lwkt_panic_dropmsg(lwkt_port_t port, lwkt_msg_t msg)
{
    panic("lwkt_dropmsg() is illegal on port %p msg %p", port, msg);
    /* NOT REACHED */
    return (ENOENT);
}
