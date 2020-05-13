/*
 * Copyright (c) 2020 François Tigeot <ftigeot@wolfpond.org>
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

#include <linux/interrupt.h>
#include <linux/slab.h>

#include <sys/kthread.h>

/*
 * Linux tasklet constraints:
 * - tasklets that have the same type cannot be run on multiple processors at
 *   the same time
 * - tasklets always run on the processor from which they were originally
 *   submitted
 * - when a tasklet is scheduled, its state is set to TASKLET_STATE_SCHED,
 *   and the tasklet added to a queue
 * - during the execution of its function, the tasklet state is set to
 *   TASKLET_STATE_RUN and the TASKLET_STATE_SCHED state is removed
 */

struct tasklet_entry {
	struct tasklet_struct *ts;
	STAILQ_ENTRY(tasklet_entry) tasklet_entries;
};

static struct lock tasklet_lock = LOCK_INITIALIZER("dltll", 0, LK_CANRECURSE);

static struct thread *tasklet_td = NULL;
STAILQ_HEAD(tasklet_list_head, tasklet_entry) tlist = STAILQ_HEAD_INITIALIZER(tlist);
STAILQ_HEAD(tasklet_hi_list_head, tasklet_entry) tlist_hi = STAILQ_HEAD_INITIALIZER(tlist_hi);

static int tasklet_pending = 0;

#define PROCESS_TASKLET_LIST(which_list) do { \
	STAILQ_FOREACH_MUTABLE(te, &which_list, tasklet_entries, tmp_te) { \
		struct tasklet_struct *t = te->ts;			\
									\
		/*							\
		   This tasklet is dying, remove it from the list.	\
		   We allow to it to run one last time if it has	\
		   already been scheduled.				\
		*/							\
		if (test_bit(TASKLET_IS_DYING, &t->state)) {		\
			STAILQ_REMOVE(&which_list, te, tasklet_entry, tasklet_entries); \
			kfree(te);					\
		}							\
									\
		/* This tasklet is not enabled, try the next one */	\
		if (atomic_read(&t->count) != 0)			\
			continue;					\
									\
		/* This tasklet is not scheduled, try the next one */	\
		if (!test_bit(TASKLET_STATE_SCHED, &t->state))		\
			continue;					\
									\
		clear_bit(TASKLET_STATE_SCHED, &t->state);		\
		set_bit(TASKLET_STATE_RUN, &t->state);			\
									\
		lockmgr(&tasklet_lock, LK_RELEASE);			\
		if (t->func)						\
			t->func(t->data);				\
		lockmgr(&tasklet_lock, LK_EXCLUSIVE);			\
									\
		clear_bit(TASKLET_STATE_RUN, &t->state);		\
	}								\
} while (0)

/* XXX runners should be CPU-specific */
static void
tasklet_runner(void *arg)
{
	struct tasklet_entry *te, *tmp_te;

	lockmgr(&tasklet_lock, LK_EXCLUSIVE);
	while (1) {
		/*
		   Only sleep if we haven't been raced by a _schedule()
		   call during an unlock window
		*/
		if (tasklet_pending == 0) {
			lksleep(&tasklet_runner, &tasklet_lock, 0, "tkidle", 0);
		}
		tasklet_pending = 0;

		/* Process hi tasklets first */
		PROCESS_TASKLET_LIST(tlist_hi);
		PROCESS_TASKLET_LIST(tlist);
	}
	lockmgr(&tasklet_lock, LK_RELEASE);
}

void
tasklet_init(struct tasklet_struct *t,
	     void (*func)(unsigned long), unsigned long data)
{
	t->state = 0;
	t->func = func;
	t->data = data;
	atomic_set(&t->count, 0);
}

#define TASKLET_SCHEDULE_COMMON(t, list) do {			\
	struct tasklet_entry *te;				\
								\
	lockmgr(&tasklet_lock, LK_EXCLUSIVE);			\
	set_bit(TASKLET_STATE_SCHED, &t->state);		\
								\
	STAILQ_FOREACH(te, &(list), tasklet_entries) {		\
		if (te->ts == t)				\
			goto found_and_done;			\
	}							\
								\
	te = kzalloc(sizeof(struct tasklet_entry), M_WAITOK);	\
	te->ts = t;						\
	STAILQ_INSERT_TAIL(&(list), te, tasklet_entries);	\
								\
found_and_done:							\
	tasklet_pending = 1;					\
	wakeup(&tasklet_runner);				\
	lockmgr(&tasklet_lock, LK_RELEASE);			\
} while (0)

void
tasklet_schedule(struct tasklet_struct *t)
{
	TASKLET_SCHEDULE_COMMON(t, tlist);
}

void
tasklet_hi_schedule(struct tasklet_struct *t)
{
	TASKLET_SCHEDULE_COMMON(t, tlist_hi);
}

void
tasklet_kill(struct tasklet_struct *t)
{
	set_bit(TASKLET_IS_DYING, &t->state);
	wakeup(&tasklet_runner);
}

static int init_tasklets(void *arg)
{
	kthread_create(tasklet_runner, NULL, &tasklet_td, "tasklet_runner");

	return 0;
}

SYSINIT(linux_tasklet_init, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, init_tasklets, NULL);
