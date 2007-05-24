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
 * $DragonFly: src/sys/kern/lwkt_msgport.c,v 1.42 2007/05/24 20:51:16 dillon Exp $
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
#include <sys/spinlock2.h>

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
lwkt_domsg(lwkt_port_t port, lwkt_msg_t msg, int flags)
{
    int error;

    KKASSERT(msg->ms_reply_port != NULL &&
	     (msg->ms_flags & (MSGF_DONE|MSGF_QUEUED)) == MSGF_DONE);
    msg->ms_flags &= ~(MSGF_REPLY | MSGF_DONE);
    msg->ms_flags |= MSGF_SYNC;
    if ((error = lwkt_beginmsg(port, msg)) == EASYNC) {
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

/************************************************************************
 *			PORT INITIALIZATION API				*
 ************************************************************************/

static void *lwkt_thread_getport(lwkt_port_t port);
static int lwkt_thread_putport(lwkt_port_t port, lwkt_msg_t msg);
static int lwkt_thread_waitmsg(lwkt_msg_t msg, int flags);
static void *lwkt_thread_waitport(lwkt_port_t port, int flags);
static void lwkt_thread_replyport(lwkt_port_t port, lwkt_msg_t msg);

static void *lwkt_spin_getport(lwkt_port_t port);
static int lwkt_spin_putport(lwkt_port_t port, lwkt_msg_t msg);
static int lwkt_spin_waitmsg(lwkt_msg_t msg, int flags);
static void *lwkt_spin_waitport(lwkt_port_t port, int flags);
static void lwkt_spin_replyport(lwkt_port_t port, lwkt_msg_t msg);

static void lwkt_null_replyport(lwkt_port_t port, lwkt_msg_t msg);
static void *lwkt_panic_getport(lwkt_port_t port);
static int lwkt_panic_putport(lwkt_port_t port, lwkt_msg_t msg);
static int lwkt_panic_waitmsg(lwkt_msg_t msg, int flags);
static void *lwkt_panic_waitport(lwkt_port_t port, int flags);
static void lwkt_panic_replyport(lwkt_port_t port, lwkt_msg_t msg);

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
	       void (*rportfn)(lwkt_port_t, lwkt_msg_t))
{
    bzero(port, sizeof(*port));
    TAILQ_INIT(&port->mp_msgq);
    port->mp_getport = gportfn;
    port->mp_putport = pportfn;
    port->mp_waitmsg =  wmsgfn;
    port->mp_waitport =  wportfn;
    port->mp_replyport = rportfn;
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
		   lwkt_thread_replyport);
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
lwkt_initport_spin(lwkt_port_t port)
{
    _lwkt_initport(port,
		   lwkt_spin_getport,
		   lwkt_spin_putport,
		   lwkt_spin_waitmsg,
		   lwkt_spin_waitport,
		   lwkt_spin_replyport);
    spin_init(&port->mpu_spin);
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
		   lwkt_null_replyport);
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
			 rportfn);
}

void
lwkt_initport_putonly(lwkt_port_t port,
		      int (*pportfn)(lwkt_port_t, lwkt_msg_t))
{
    _lwkt_initport(port, lwkt_panic_getport, pportfn,
			 lwkt_panic_waitmsg, lwkt_panic_waitport,
			 lwkt_panic_replyport);
}

void
lwkt_initport_panic(lwkt_port_t port)
{
    _lwkt_initport(port,
		   lwkt_panic_getport, lwkt_panic_putport,
		   lwkt_panic_waitmsg, lwkt_panic_waitport,
		   lwkt_panic_replyport);
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

#ifdef SMP

/*
 * This function completes reply processing for the default case in the
 * context of the originating cpu.
 */
static
void
lwkt_thread_replyport_remote(lwkt_msg_t msg)
{
    lwkt_port_t port = msg->ms_reply_port;

    /*
     * Chase any thread migration that occurs
     */
    if (port->mpu_td->td_gd != mycpu) {
	lwkt_send_ipiq(port->mpu_td->td_gd,
		       (ipifunc1_t)lwkt_thread_replyport_remote, msg);
	return;
    }

    /*
     * Cleanup
     */
#ifdef INVARIANTS
    KKASSERT(msg->ms_flags & MSGF_INTRANSIT);
    msg->ms_flags &= ~MSGF_INTRANSIT;
#endif
    if (msg->ms_flags & MSGF_SYNC) {
	    msg->ms_flags |= MSGF_REPLY | MSGF_DONE;
    } else {
	    TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	    msg->ms_flags |= MSGF_REPLY | MSGF_DONE | MSGF_QUEUED;
    }
    if (port->mp_flags & MSGPORTF_WAITING)
	lwkt_schedule(port->mpu_td);
}

#endif

/*
 * lwkt_thread_replyport() - Backend to lwkt_replymsg()
 *
 * Called with the reply port as an argument but in the context of the
 * original target port.  Completion must occur on the target port's
 * cpu.
 *
 * The critical section protects us from IPIs on the this CPU.
 */
void
lwkt_thread_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    KKASSERT((msg->ms_flags & (MSGF_DONE|MSGF_QUEUED)) == 0);

    if (msg->ms_flags & MSGF_SYNC) {
	/*
	 * If a synchronous completion has been requested, just wakeup
	 * the message without bothering to queue it to the target port.
	 *
	 * Assume the target thread is non-preemptive, so no critical
	 * section is required.
	 */
#ifdef SMP
	if (port->mpu_td->td_gd == mycpu) {
#endif
	    msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
	    if (port->mp_flags & MSGPORTF_WAITING)
		lwkt_schedule(port->mpu_td);
#ifdef SMP
	} else {
#ifdef INVARIANTS
	    msg->ms_flags |= MSGF_INTRANSIT;
#endif
	    msg->ms_flags |= MSGF_REPLY;
	    lwkt_send_ipiq(port->mpu_td->td_gd,
			   (ipifunc1_t)lwkt_thread_replyport_remote, msg);
	}
#endif
    } else {
	/*
	 * If an asynchronous completion has been requested the message
	 * must be queued to the reply port.  MSGF_REPLY cannot be set
	 * until the message actually gets queued.
	 *
	 * A critical section is required to interlock the port queue.
	 */
#ifdef SMP
	if (port->mpu_td->td_gd == mycpu) {
#endif
	    crit_enter();
	    TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	    msg->ms_flags |= MSGF_REPLY | MSGF_DONE | MSGF_QUEUED;
	    if (port->mp_flags & MSGPORTF_WAITING)
		lwkt_schedule(port->mpu_td);
	    crit_exit();
#ifdef SMP
	} else {
#ifdef INVARIANTS
	    msg->ms_flags |= MSGF_INTRANSIT;
#endif
	    msg->ms_flags |= MSGF_REPLY;
	    lwkt_send_ipiq(port->mpu_td->td_gd,
			   (ipifunc1_t)lwkt_thread_replyport_remote, msg);
	}
#endif
    }
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

#ifdef SMP

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
     * Cleanup
     */
#ifdef INVARIANTS
    KKASSERT(msg->ms_flags & MSGF_INTRANSIT);
    msg->ms_flags &= ~MSGF_INTRANSIT;
#endif
    TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
    msg->ms_flags |= MSGF_QUEUED;
    if (port->mp_flags & MSGPORTF_WAITING)
	lwkt_schedule(port->mpu_td);
}

#endif

static
int
lwkt_thread_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    KKASSERT((msg->ms_flags & (MSGF_DONE | MSGF_REPLY)) == 0);

    msg->ms_target_port = port;
#ifdef SMP
    if (port->mpu_td->td_gd == mycpu) {
#endif
	crit_enter();
	msg->ms_flags |= MSGF_QUEUED;
	TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	if (port->mp_flags & MSGPORTF_WAITING)
	    lwkt_schedule(port->mpu_td);
	crit_exit();
#ifdef SMP
    } else {
#ifdef INVARIANTS
	msg->ms_flags |= MSGF_INTRANSIT;
#endif
	lwkt_send_ipiq(port->mpu_td->td_gd,
			(ipifunc1_t)lwkt_thread_putport_remote, msg);
    }
#endif
    return (EASYNC);
}

/*
 * lwkt_thread_getport()
 *
 *	Retrieve the next message from the port or NULL if no messages
 *	are ready.
 */
void *
lwkt_thread_getport(lwkt_port_t port)
{
    lwkt_msg_t msg;

    KKASSERT(port->mpu_td == curthread);

    crit_enter_quick(port->mpu_td);
    if ((msg = TAILQ_FIRST(&port->mp_msgq)) != NULL)
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
int
lwkt_thread_waitmsg(lwkt_msg_t msg, int flags)
{
    if ((msg->ms_flags & MSGF_DONE) == 0) {
	/*
	 * If the done bit was not set we have to block until it is.
	 */
	lwkt_port_t port = msg->ms_reply_port;
	thread_t td = curthread;
	int sentabort;

	KKASSERT(port->mpu_td == td);
	KKASSERT(msg->ms_reply_port == port);
	crit_enter_quick(td);
	sentabort = 0;

	while ((msg->ms_flags & MSGF_DONE) == 0) {
	    port->mp_flags |= MSGPORTF_WAITING;
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
	if (msg->ms_flags & MSGF_QUEUED) {
	    lwkt_port_t port = msg->ms_reply_port;
	    thread_t td = curthread;

	    KKASSERT(port->mpu_td == td);
	    KKASSERT(msg->ms_reply_port == port);
	    crit_enter_quick(td);
	    _lwkt_pullmsg(port, msg);
	    crit_exit_quick(td);
	}
    }
    return(msg->ms_error);
}

void *
lwkt_thread_waitport(lwkt_port_t port, int flags)
{
    thread_t td = curthread;
    lwkt_msg_t msg;
    int error;

    KKASSERT(port->mpu_td == td);
    crit_enter_quick(td);
    while ((msg = TAILQ_FIRST(&port->mp_msgq)) == NULL) {
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

    spin_lock_wr(&port->mpu_spin);
    if ((msg = TAILQ_FIRST(&port->mp_msgq)) != NULL)
	_lwkt_pullmsg(port, msg);
    spin_unlock_wr(&port->mpu_spin);
    return(msg);
}

static
int
lwkt_spin_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    int dowakeup;

    KKASSERT((msg->ms_flags & (MSGF_DONE | MSGF_REPLY)) == 0);

    msg->ms_target_port = port;
    spin_lock_wr(&port->mpu_spin);
    msg->ms_flags |= MSGF_QUEUED;
    TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
    dowakeup = 0;
    if (port->mp_flags & MSGPORTF_WAITING) {
	port->mp_flags &= ~MSGPORTF_WAITING;
	dowakeup = 1;
    }
    spin_unlock_wr(&port->mpu_spin);
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

    if ((msg->ms_flags & MSGF_DONE) == 0) {
	port = msg->ms_reply_port;
	sentabort = 0;
	spin_lock_wr(&port->mpu_spin);
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
		error = msleep(won, &port->mpu_spin, PCATCH, "waitmsg", 0);
		if (error) {
		    sentabort = error;
		    spin_unlock_wr(&port->mpu_spin);
		    lwkt_abortmsg(msg);
		    spin_lock_wr(&port->mpu_spin);
		}
	    } else {
		error = msleep(won, &port->mpu_spin, 0, "waitmsg", 0);
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
	spin_unlock_wr(&port->mpu_spin);
    } else {
	if (msg->ms_flags & MSGF_QUEUED) {
	    port = msg->ms_reply_port;
	    spin_lock_wr(&port->mpu_spin);
	    _lwkt_pullmsg(port, msg);
	    spin_unlock_wr(&port->mpu_spin);
	}
    }
    return(msg->ms_error);
}

static
void *
lwkt_spin_waitport(lwkt_port_t port, int flags)
{
    lwkt_msg_t msg;
    int error;

    spin_lock_wr(&port->mpu_spin);
    while ((msg = TAILQ_FIRST(&port->mp_msgq)) == NULL) {
	port->mp_flags |= MSGPORTF_WAITING;
	error = msleep(port, &port->mpu_spin, flags, "waitport", 0);
	/* see note at the top on the MSGPORTF_WAITING flag */
	if (error) {
	    spin_unlock_wr(&port->mpu_spin);
	    return(NULL);
	}
    }
    _lwkt_pullmsg(port, msg);
    spin_unlock_wr(&port->mpu_spin);
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
	 */
	msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
	wakeup(msg);
    } else {
	/*
	 * If an asynchronous completion has been requested the message
	 * must be queued to the reply port.  MSGF_REPLY cannot be set
	 * until the message actually gets queued.
	 */
	spin_lock_wr(&port->mpu_spin);
	TAILQ_INSERT_TAIL(&port->mp_msgq, msg, ms_node);
	msg->ms_flags |= MSGF_REPLY | MSGF_DONE | MSGF_QUEUED;
	dowakeup = 0;
	if (port->mp_flags & MSGPORTF_WAITING) {
	    port->mp_flags &= ~MSGPORTF_WAITING;
	    dowakeup = 1;
	}
	spin_unlock_wr(&port->mpu_spin);
	if (dowakeup)
	    wakeup(port);
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

