/*
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * Copyright (c) 2014-2020 François Tigeot <ftigeot@wolfpond.org>
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
#ifndef	_LINUX_WORKQUEUE_H_
#define	_LINUX_WORKQUEUE_H_

#include <linux/timer.h>
#include <linux/bitops.h>
#include <linux/lockdep.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>

#include <sys/taskqueue.h>

struct workqueue_struct {
	struct taskqueue	*taskqueue;
	struct lock		flags_lock;
	bool			is_draining;
};

struct work_struct {
	struct	task 		work_task;
	struct	taskqueue	*taskqueue;
	void			(*func)(struct work_struct *);
};

struct delayed_work {
	struct work_struct	work;
	struct callout		timer;
};

static inline struct delayed_work *
to_delayed_work(struct work_struct *work)
{

	return container_of(work, struct delayed_work, work);
}


static inline void
_work_fn(void *context, int pending)
{
	struct work_struct *work;

	work = context;
	work->func(work);
}

#define	INIT_WORK(work, _func) 	 					\
do {									\
	(work)->func = (_func);						\
	(work)->taskqueue = NULL;					\
	TASK_INIT(&(work)->work_task, 0, _work_fn, (work));		\
} while (0)

#define INIT_WORK_ONSTACK(work, _func)	INIT_WORK(work, _func)

#define INIT_DELAYED_WORK(_work, _func)					\
do {									\
	INIT_WORK(&(_work)->work, _func);				\
	callout_init_mp(&(_work)->timer);				\
} while (0)

#define	INIT_DEFERRABLE_WORK	INIT_DELAYED_WORK

#define	schedule_work(work)						\
do {									\
	taskqueue_enqueue_optq(taskqueue_thread[mycpuid], &(work)->taskqueue, &(work)->work_task);	\
} while (0)

#define	flush_scheduled_work()	flush_taskqueue(taskqueue_thread[mycpuid])

static inline int queue_work(struct workqueue_struct *q, struct work_struct *work)
{
	/* Return opposite val to align with Linux logic */
	return !taskqueue_enqueue_optq((q)->taskqueue, &(work)->taskqueue, &(work)->work_task);
}

static inline void
_delayed_work_fn(void *arg)
{
	struct delayed_work *work;

	work = arg;
	taskqueue_enqueue_optq(work->work.taskqueue, &work->work.taskqueue, &work->work.work_task);
}

static inline int
queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *work,
    unsigned long delay)
{
	int pending;

	pending = work->work.work_task.ta_pending;
	work->work.taskqueue = wq->taskqueue;
	if (delay != 0) {
		callout_reset(&work->timer, delay, _delayed_work_fn, work);
	} else {
		_delayed_work_fn((void *)work);
	}

	return (!pending);
}

static inline bool schedule_delayed_work(struct delayed_work *dwork,
                                         unsigned long delay)
{
        struct workqueue_struct wq;

        wq.taskqueue = taskqueue_thread[mycpuid];
        return queue_delayed_work(&wq, dwork, delay);
}

struct workqueue_struct * _create_workqueue_common(char *name, int cpus);

#define	create_singlethread_workqueue(name)				\
	_create_workqueue_common(name, 1)

#define	create_workqueue(name)						\
	_create_workqueue_common(name, MAXCPU)

#define alloc_ordered_workqueue(name, flags)				\
	_create_workqueue_common(name, 1)

#define alloc_workqueue(name, flags, max_active)			\
	_create_workqueue_common(name, max_active)

void destroy_workqueue(struct workqueue_struct *wq);

#define	flush_workqueue(wq)	flush_taskqueue((wq)->taskqueue)

static inline void
_flush_fn(void *context, int pending)
{
}

static inline void
flush_taskqueue(struct taskqueue *tq)
{
	struct task flushtask;

	TASK_INIT(&flushtask, 0, _flush_fn, NULL);
	taskqueue_enqueue(tq, &flushtask);
	taskqueue_drain(tq, &flushtask);
}

static inline int
cancel_work_sync(struct work_struct *work)
{
	if (taskqueue_cancel_simple(&work->work_task))
		taskqueue_drain_simple(&work->work_task);
	return 0;
}

/*
 * This may leave work running on another CPU as it does on Linux.
 */
static inline int
cancel_delayed_work(struct delayed_work *work)
{
	callout_stop(&work->timer);
	return (taskqueue_cancel_simple(&work->work.work_task) == 0);
}

static inline int
cancel_delayed_work_sync(struct delayed_work *work)
{
	callout_cancel(&work->timer);
	if (taskqueue_cancel_simple(&work->work.work_task))
		taskqueue_drain_simple(&work->work.work_task);
	return 0;
}

static inline bool
mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
		                      unsigned long delay)
{
	cancel_delayed_work(dwork);
	queue_delayed_work(wq, dwork, delay);
	return false;
}

static inline bool
flush_work(struct work_struct *work)
{
	taskqueue_drain_simple(&work->work_task);

	return true;
}

static inline void
destroy_work_on_stack(struct work_struct *work)
{
}

/* System-wide workqueues */
extern struct workqueue_struct *system_wq;
extern struct workqueue_struct *system_highpri_wq;
extern struct workqueue_struct *system_long_wq;
extern struct workqueue_struct *system_unbound_wq;
extern struct workqueue_struct *system_power_efficient_wq;

static inline unsigned int
work_busy(struct work_struct *work)
{
	/* Just pretend nothing is busy, this function is unreliable anyway */
	return 0;
}

bool flush_delayed_work(struct delayed_work *dwork);

void drain_workqueue(struct workqueue_struct *wq);

static inline bool
work_pending(struct work_struct * work)
{
	return work->work_task.ta_pending;
}

#endif	/* _LINUX_WORKQUEUE_H_ */
