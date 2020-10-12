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
 * destruction, repurposing, or reuse of system structures against concurrent
 * accessors on other cpus.
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

#include <sys/lock.h>
#include <machine/cpumask.h>

struct thread;

struct exislock {
	long		pseudo_ticks;
};
typedef struct exislock exislock_t;

#define EXIS_THRESH	4
#define EXIS_LIVE	(EXIS_THRESH*2)

extern long pseudo_ticks;

/*
 * Initialize the structure
 */
static __inline void
exis_init(exislock_t *xlk)
{
	xlk->pseudo_ticks = 0;
}

/*
 * pcpu exis lock API
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
 * Issue terminate on object, return TRUE if this was the first terminate
 * on the object.  If multiple terminations are issued on an object, then
 * xlk->pseudo_ticks may be updated multiple times.
 *
 * Callers should ensure that multiple terminations only occur due to
 * concurrency, with exis_hold() held.  If the caller can ensure only one
 * termination will occur, exis_hold() does not need to be held.
 */
static __inline long
exis_terminate(exislock_t *xlk)
{
	long val;

	val = atomic_swap_long(&xlk->pseudo_ticks, pseudo_ticks + EXIS_THRESH);
	return (val == 0);
}

/*
 * After issuing a terminate, poll whether the object can now be destroyed,
 * repurposed, or reused.
 *
 * Returns the number of pseudo_ticks remaining until the object can be
 * destroyed, repurposed, or reused.  Caller must re-poll after calling
 * exis_stall().
 *
 * Returns <= 0 if the object can be immediately destroyed, repurposed, or
 * reused.
 *
 * Returns EXIS_LIVE if the object has not been terminated.
 */
static __inline long
exis_poll(exislock_t *xlk)
{
	long val = xlk->pseudo_ticks;

	cpu_ccfence();
	if (val == 0)
		return EXIS_LIVE;
	return (pseudo_ticks - val);
}

/*
 * Delay (val) pseudo_ticks (pseudo_ticks increment at half the rate that
 * ticks increments if not held up by pcpu exis_hold() conditions).  No delay
 * will occur if val is less than or equal to 0.
 *
 * Caller cannot assume that the specified number of pseudo_ticks has occurred
 * and should re-poll any structures in question.
 */
static __inline void
exis_stall(long val)
{
	int dummy;

	if (val > 0)
		tsleep(&dummy, 0, "exisw", val*2);
}
