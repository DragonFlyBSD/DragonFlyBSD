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
/*
 * Existential Lock implementation
 *
 * This implements an API which allows consumers to safely manage the
 * caching, destruction, repurposing, or reuse of system structures against
 * concurrent accessors on other cpus without the use of locks or cache
 * line bounces in the critical path.
 *
 * An accessor wraps the critical code with an exis_hold() and exis_drop()
 * pair.  This increments a contention-free per-cpu ref-count which prevents
 * the pseudo_ticks global from incrementing more than once.  The accessor
 * can block for short periods of time inside the code block (but should not
 * block indefinitely as overlapping accessors may then prevent pseudo_ticks
 * from ever incrementing!).
 *
 * The subsystem which intends to destroy, repurpose, or reuse the structure
 * first unlinks the structure from whatever data structures accessors use
 * to find it, and then issues exis_terminate() on the exislock for the
 * structure.  The subsystem can then call exis_poll() on the exislock to
 * determine when it is safe to destroy, repurpose, or reuse the structure.
 * This will occur EXIS_THRESH pseudo_ticks after the exis_terminate() call.
 */

/*
 * API (see sys/exislock2.h)
 *
 * exis_hold()
 * exis_drop()
 *
 *	Wrap type-safe critical code, preventing the global pseudo_ticks
 *	counter from incrementing more than once.  The counter may still
 *	increment zero or one time.
 *
 *	The code may block, but should not block forever since it will
 *	prevent state transitions for the type-safe API across the entire
 *	system.
 *
 *	All other API functions (except exis_init() for intitializing a
 *	new structure from scratch) should only be called while a type-safe
 *	critical section is held.
 *
 * exis_init()
 *
 *	Unconditionally initialize the exis structure, placing it in a
 *	EXIS_LIVE state.
 *
 * exis_poll()
 *
 *	Return the pseudo_ticks delta relative to an exis structure.  A
 *	value >= 0 indicates that the structure is LIVE or CACHED.  A negative
 *	value indicates that the structure is not usable.  -1 indicates
 *	NOTCACHED and <= -2 indicates TERMINATE (freeable).
 *
 *	Values should not generally be interpreted beyond this.  Instead,
 *	use exis_state(), exis_usable(), and exis_freeable().
 *
 * exis_state()
 *
 *	Returns the current EXIS_* state of the structure.  EXIS_TERMINATE,
 *	EXIS_NOTCACHED, EXIS_CACHED, or EXIS_LIVE.  This macro has a bit of
 *	complexity to it and is not usually called in the critical path.
 *	Instead, exis_isusable() is typically called.
 *
 * exis_usable()
 *
 *	Returns non-zero if the exis structure is usable, meaning that it
 *	is in a LIVE or CACHED state (>= 0).
 *
 * exis_freeable()
 *
 *	Returns non-zero if the exis structure is usable, meaning that it
 *	is in a TERMINATE state (<= -2).
 *
 * exis_cache(N)
 *
 *	Transitions an exis structure from LIVE or CACHED to CACHED and
 *	refreshes the time the structure will remain cached to approximately
 *	(N) pseudo_ticks.  Returns TRUE in this case.
 *
 *	A structure can transition from CACHED to NOTCACHED while a
 *	type-safe critical section is held, due to the possibility of
 *	pseudo_ticks incrementing once.  To handle this case, any
 *	structure in a NOTCACHED state will be changed backed to a
 *	CACHED state for approximtely (N) pseudo_ticks and TRUE will be
 *	returned.
 *
 *	WARNING! It is extremely important that exis_cache(N) not be called
 *	on a structure that was polled to be in a NOTCACHED state.  The only
 *	case where this is allowed is if the structure was originally found
 *	to be in a CACHED state and then concurrently transitioned to
 *	NOTCACHED.  THAT race is ok, but an initial NOTCACHED state is not
 *	ok.  The reason is that the NOTCACHED state is used to sequence
 *	multiple TERMINATE stages (if the caller desires this).
 *
 *	A structure in any other state will not be modified and FALSE will
 *	be returned.
 *
 *	The value (0) can be passed to cause the structure to enter into
 *	a CACHED state with the least timeout.
 *
 * exis_terminate()
 *
 *	If the structure is in a LIVE state the structure will be set to
 *	a CACHED(0) state and EXIS_LIVE will be returned.  The caller
 *	can return the structure to a LIVE state if desired but should
 *	otherwise take no action.
 *
 *	If the structure is in a CACHED state the structure will be set to
 *	a CACHED(0) state and EXIS_CACHED will be returned.  The caller
 *	can return the structure to a LIVE or CACHED state if desired
 *	but should otherwise take no action.
 *
 *	A structure will be in a NOTCACHED state for 2 pseudo ticks after
 *	leaving the CACHED state.  EXIS_NOTCACHED is returned and the caller
 *	should not take any action on the structure.
 *
 * 				TERMINATION SEQUENCING
 *
 *	After 1-2 ticks in the NOTCACHED state, the structure enters the
 *	EXIS_TERMINATE state.  Note that it may still be connected to
 *	system data structures at this time and full concurrent access
 *	requires some termination staging.
 *
 *	exis_terminate() will return EXIS_TERMINATE but also set the exis
 *	structure back to a NOTCACHED state for 1-2 ticks.  The caller
 *	typically must hold a strong lock on the structure and/or related
 *	data structures to properly terminate it.
 *
 *	--- For the first TERMINATE, the caller removes the object from
 *	    system data structures.  It is important that the caller
 *	    NOT DESTROY the type-safe nature of the object at this time.
 *
 *	--- For the second TERMINATE, the caller may destroy the object.
 *	    The caller usually distinguishes the two states with a flag
 *	    or checking pointer fields or whatnot.
 *
 *	--- Additional TERMINATE states may be sequenced if necessary, there
 *	    is no limit.
 *
 * 				REACTIVATION SEQUENCING
 *
 *	Structures can remain in memory, unaccessed, for long periods of time.
 *	This can cause the structure to wind up in an EXIS_TERMINATE state.
 *	The result is that stronger locks will be needed when looking up the
 *	structure and the code doing this can then re-activate the EXIS state
 *	by issuing exis_init(), and then re-cache (if desired) it via
 *	exis_cache().
 *
 *	Repeated lookups of the same structure are likely to remain cached
 *	for the caching period (N ticks, via the API) before having to refresh
 *	the structure again.  Or indefinitely if the exis lock is in the
 *	LIVE state.
 *
 *	Thus an EXIS_TERMIANTE state does *NOT* necessarily mean that the
 *	caller actually intends to terminate the structure, only that stronger
 *	locking will be required to return it to its fast-access state.
 */

#ifndef _SYS_EXISLOCK_H_
#define	_SYS_EXISLOCK_H_

struct exislock {
	long		pseudo_ticks;
};
typedef struct exislock exislock_t;

typedef enum exis_state {
	EXIS_TERMINATE,
	EXIS_NOTCACHED,
	EXIS_CACHED,
	EXIS_LIVE
} exis_state_t;

extern long pseudo_ticks;

#endif /* !_SYS_EXISLOCK_H_ */
