/*
 * Copyright (c) 2017 François Tigeot
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_INTERRUPT_H_
#define _LINUX_INTERRUPT_H_

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/irqflags.h>
#include <linux/hardirq.h>
#include <linux/hrtimer.h>
#include <linux/kref.h>

#include <linux/atomic.h>

struct tasklet_struct {
	unsigned long state;
	void (*func)(unsigned long);
	unsigned long data;
	struct lock lock;
};

enum {
	TASKLET_STATE_SCHED,
	TASKLET_STATE_RUN
};

/*
 * TODO: verify these points:
 * - tasklets that have the same type cannot be run on multiple processors at the same time
 * - tasklets always run on the processor from which they were originally
 *   submitted
 * - when a tasklet is scheduled, its state is set to TASKLET_STATE_SCHED, and the tasklet
 *   added to a queue
 * - during the execution of its function, the tasklet state is set to TASKLET_STATE_RUN
 *   and the TASKLET_STATE_SCHED state is removed
 */

/* XXX scheduling and execution should be handled separately */
static inline void
tasklet_schedule(struct tasklet_struct *t)
{
	set_bit(TASKLET_STATE_SCHED, &t->state);

	lockmgr(&t->lock, LK_EXCLUSIVE);
	clear_bit(TASKLET_STATE_SCHED, &t->state);

	set_bit(TASKLET_STATE_RUN, &t->state);
	if (t->func)
		t->func(t->data);
	clear_bit(TASKLET_STATE_RUN, &t->state);

	lockmgr(&t->lock, LK_RELEASE);
}

/* This function ensures that the tasklet is not scheduled to run again */
/* XXX this doesn't kill anything */
static inline void
tasklet_kill(struct tasklet_struct *t)
{
	lockmgr(&t->lock, LK_EXCLUSIVE);
	clear_bit(TASKLET_STATE_SCHED, &t->state);
	lockmgr(&t->lock, LK_RELEASE);
}

static inline void
tasklet_init(struct tasklet_struct *t, void (*func)(unsigned long), unsigned long data)
{
	lockinit(&t->lock, "ltasklet", 0, LK_CANRECURSE);
	t->state = 0;
	t->func = func;
	t->data = data;
}

#endif	/* _LINUX_INTERRUPT_H_ */
