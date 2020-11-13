/*
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
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
#ifndef _SYS_EXISLOCK2_H_
#define	_SYS_EXISLOCK2_H_

#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>
#endif
#ifndef _MACHINE_THREAD_H_
#include <machine/thread.h>
#endif

/*
 * Initialize the structure
 */
static __inline void
exis_init(exislock_t *xlk)
{
	xlk->pseudo_ticks = 0;
}

/*
 * pcpu exis lock API.  Enter and and exit a type-safe critical section.
 */
static __inline void
exis_hold_gd(globaldata_t gd)
{
	++gd->gd_exislockcnt;
}

static __inline void
exis_drop_gd(globaldata_t gd)
{
	if (--gd->gd_exislockcnt == 0)
		gd->gd_exisarmed = 1;
}

static __inline void
exis_hold(void)
{
	exis_hold_gd(mycpu);
}

static __inline void
exis_drop(void)
{
	exis_drop_gd(mycpu);
}

/*
 * poll whether the object is usable or not.  A value >= 0 indicates that
 * the (possibly cached) object is usable.
 *
 * This call returns the approximate number of pseudo_ticks remaining until
 * the object becomes unusable, +/- one.
 *
 * The actual value returns is either >= 0, or a negative number.  Caller
 * should refrain from trying to interpret values >= 0 other than the fact
 * that they are >= 0.
 *
 * Negative numbers indicate the number of pseudo_ticks which have occurred
 * since the object became unusable.  Various negative values trigger
 * different actions.
 */
static __inline long
exis_poll(exislock_t *xlk)
{
	long val = xlk->pseudo_ticks;

	cpu_ccfence();
	if (val == 0)
		return val;
	return (val - pseudo_ticks);
}

/*
 * Return the current state.  Note that the NOTCACHED state persists for
 * two pseudo_ticks.  This is done because the global pseudo_ticks counter
 * can concurrently increment by 1 (but no more than 1) during a type-safe
 * critical section.
 *
 * The state can transition even while holding a type-safe critical section,
 * but sequencing is designed such that this does not cause any problems.
 */
static __inline int
exis_state(exislock_t *xlk)
{
	long val = xlk->pseudo_ticks;

	cpu_ccfence();
	if (val == 0)
		return EXIS_LIVE;
	val = val - pseudo_ticks;
	if (val >= 0)
		return EXIS_CACHED;
	if (val >= -2)
		return EXIS_NOTCACHED;
	return EXIS_TERMINATE;
}

/*
 * Returns non-zero if the structure is usable (either LIVE or CACHED).
 *
 * WARNING! The structure is not considered to be usable if it is in
 *	    an UNCACHED state, but if it is CACHED and transitions to
 *	    UNCACHED during a type-safe critical section it does remain
 *	    usable for the duration of that type-safe critical section.
 */
static __inline int
exis_usable(exislock_t *xlk)
{
	return (exis_poll(xlk) >= 0);
}

/*
 * Returns non-zero if the structure can be destroyed
 */
static __inline int
exis_freeable(exislock_t *xlk)
{
	return (exis_poll(xlk) <= -2);
}

/*
 * If the structure is in a LIVE or CACHED state, or if it was CACHED and
 * concurrently transitioned to NOTCACHED in the same type-safe critical
 * section, the state will be reset to a CACHED(n) state and non-zero is
 * returned.
 *
 * Otherwise 0 is returned and no action is taken.
 */
static __inline int
exis_cache(exislock_t *xlk, long n)
{
	long val = xlk->pseudo_ticks;
	long pticks = pseudo_ticks;

	cpu_ccfence();
	if (val)
		val = val - pticks;
	if (val >= -1) {
		/*
		 * avoid cache line ping-pong
		 */
		pticks += n + 1;
		if (xlk->pseudo_ticks != pticks) {
			cpu_ccfence();
			xlk->pseudo_ticks = pticks;
		}
		return 1;
	}
	return 0;
}

/*
 * Termination sequencing.
 *
 * The structure is placed in a CACHED(0) state if LIVE or CACHED.
 * The NOTCACHED state should not be acted upon by the caller until
 * and unless it transitions to TERMINATE.
 *
 * Upon returning EXIS_TERMINATE, the structure is returned to a
 * NOTCACHED state and another 1-2 pseudo ticks will pass until it goes
 * back to EXIS_TERMINATE (if needed by the caller).  Once the caller
 * is fully satisfied, it may repurpose or destroy the structure.
 *
 * Caller should hold a strong interlock on the structure in addition
 * to being in a type-safe critical section.
 */
static __inline exis_state_t
exis_terminate(exislock_t *xlk)
{
	exis_state_t state;

	state = exis_state(xlk);
	switch(state) {
	case EXIS_TERMINATE:
		/*
		 * Set to NOTCACHED state and return EXIS_TERMINATE.
		 * due to pseudo_ticks races, the NOTCACHED state will
		 * persist for 1-2 pseudo ticks.
		 */
		xlk->pseudo_ticks = pseudo_ticks - 1;
		state = EXIS_TERMINATE;
		break;
	case EXIS_NOTCACHED:
		break;
	case EXIS_CACHED:
	case EXIS_LIVE:
		xlk->pseudo_ticks = pseudo_ticks;
		break;
	}
	return state;
}

#endif /* !_SYS_EXISLOCK2_H_ */
