/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
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
 */
/*
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 *
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_CALLOUT_H_
#define _SYS_CALLOUT_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_LOCK_H_
#include <sys/lock.h>
#endif
#ifndef _CPU_ATOMIC_H_
#include <machine/atomic.h>
#endif

SLIST_HEAD(callout_list, callout);
TAILQ_HEAD(callout_tailq, callout);

/*
 * Callwheel linkages are only adjusted on the target cpu.  The target
 * cpu can only be [re]assigned when the IPI_MASK and PENDING bits are
 * clear.
 *
 * callout_reset() and callout_stop() are always synchronous and will
 * interlock against a running callout as well as reassign the callout
 * to the current cpu.  The caller might block, and a deadlock is possible
 * if the caller does not use callout_init_lk() or is not careful with
 * locks acquired in the callout function.
 *
 * Programers should note that our lockmgr locks have a cancelation feature
 * which can be used to avoid deadlocks.  callout_init_lk() also uses this
 * feature.
 *
 * callout_deactivate() is asynchronous and will not interlock against
 * anything.  Deactivation does not dequeue a callout, it simply prevents
 * its function from being executed.
 */
struct callout {
	union {
		SLIST_ENTRY(callout) sle;
		TAILQ_ENTRY(callout) tqe;
	} c_links;
	int	c_time;			/* match tick on event */
	int	c_load;			/* load value for reset ipi */
	void	*c_arg;			/* function argument */
	void	(*c_func) (void *);	/* function to call */
	int	c_flags;		/* state of this entry */
	int	c_unused02;
	struct lock *c_lk;		/* auto-lock */
};

/*
 * ACTIVE	- If cleared this the callout is prevented from issuing its
 *		  callback.  The callout remains on its timer queue.
 *
 * PENDING	- Indicates the callout is on a particular cpu's timer queue.
 *		  Also locks the cpu owning the callout.
 *
 * MPSAFE	- Indicates the callout does not need the MP lock (most
 *		  callouts are flagged this way).
 *
 * DID_INIT	- Safety
 *
 * EXECUTED	- Set prior to function dispatch, cleared by callout_reset(),
 *		  cleared and (prior value) returned by callout_stop_sync().
 *
 * WAITING	- Used for tsleep/wakeup blocking, primarily for
 *		  callout_stop().
 *
 * IPI_MASK	- Counts pending IPIs.  Also locks the cpu owning the callout.
 *
 * CPU_MASK	- Currently assigned cpu.  Only valid when at least one bit
 *		  in ARMED_MASK is set.
 *
 */
#define CALLOUT_ACTIVE		0x80000000 /* quick [de]activation flag */
#define CALLOUT_PENDING		0x40000000 /* callout is on callwheel */
#define CALLOUT_MPSAFE		0x20000000 /* callout does not need the BGL */
#define CALLOUT_DID_INIT	0x10000000 /* safety check */
#define CALLOUT_AUTOLOCK	0x08000000 /* auto locking / cancel feature */
#define CALLOUT_WAITING		0x04000000 /* interlocked waiter */
#define CALLOUT_EXECUTED	0x02000000 /* (generates stop status) */
#define CALLOUT_UNUSED01	0x01000000
#define CALLOUT_IPI_MASK	0x00000FFF /* count operations in prog */
#define CALLOUT_CPU_MASK	0x00FFF000 /* cpu assignment */

#define CALLOUT_ARMED_MASK	(CALLOUT_PENDING | CALLOUT_IPI_MASK)

#define CALLOUT_FLAGS_TO_CPU(flags)	(((flags) & CALLOUT_CPU_MASK) >> 12)
#define CALLOUT_CPU_TO_FLAGS(cpuid)	((cpuid) << 12)

/*
 * WARNING! The caller is responsible for stabilizing the callout state,
 *	    our suggestion is to either manage the callout on the same cpu
 *	    or to use the callout_init_lk() feature and hold the lock while
 *	    making callout_*() calls.  The lock will be held automatically
 *	    by the callout wheel for any call-back and the callout wheel
 *	    will handle any callout_stop() deadlocks properly.
 *
 * active  -	Returns activation status.  This bit is set by callout_reset*()
 *		and will only be cleared by an explicit callout_deactivate()
 *		or callout_stop().  A function dispatch does not clear this
 *		bit.  In addition, a callout_reset() to another cpu is
 *		asynchronous and may not immediately re-set this bit.
 *
 * deactivate -	Disarm the callout, preventing it from being executed if it
 *		is queued or the queueing operation is in-flight.  Has no
 *		effect if the callout has already been dispatched.  Does not
 *		dequeue the callout.  Does not affect the status returned
 *		by callout_stop().
 *
 *		Not serialized, caller must be careful when racing a new
 *		callout_reset() that might be issued by the callback, which
 *		will re-arm the callout.
 *
 *		callout_reset() must be called to reactivate the callout.
 *
 * pending -	Only useful for same-cpu callouts, indicates that the callout
 *		is pending on the callwheel or that a callout_reset() ipi
 *		is (probably) in-flight.  Can also false-positive on
 *		callout_stop() IPIs.
 */
#define	callout_active(c)	((c)->c_flags & CALLOUT_ACTIVE)

#define	callout_deactivate(c)	atomic_clear_int(&(c)->c_flags, CALLOUT_ACTIVE)

#define	callout_pending(c)	((c)->c_flags & CALLOUT_ARMED_MASK)

#ifdef _KERNEL
extern int	ncallout;

struct globaldata;

void	hardclock_softtick(struct globaldata *);
void	callout_init (struct callout *);
void	callout_init_mp (struct callout *);
void	callout_init_lk (struct callout *, struct lock *);
void	callout_reset (struct callout *, int, void (*)(void *), void *);
int	callout_stop (struct callout *);
void	callout_stop_async (struct callout *);
int	callout_stop_sync (struct callout *);
void	callout_terminate (struct callout *);
void	callout_reset_bycpu (struct callout *, int, void (*)(void *), void *,
	    int);

#define	callout_drain(x) callout_stop_sync(x)

#endif

#endif /* _SYS_CALLOUT_H_ */
