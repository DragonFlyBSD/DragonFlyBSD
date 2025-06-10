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

#define WQ_HIGHPRI	1
#define WQ_FREEZABLE	2
#define WQ_UNBOUND	4

struct workqueue_worker;

struct work_struct {
	STAILQ_ENTRY(work_struct) ws_entries;
	void	(*func)(struct work_struct *);
	struct workqueue_worker *worker;
	bool	on_queue;
	bool	running;
	bool	canceled;
};

struct workqueue_worker {
	STAILQ_HEAD(ws_list, work_struct) ws_list_head;
	struct thread *worker_thread;
	struct lock worker_lock;
};

struct workqueue_struct {
	bool	is_draining;
	int	num_workers;
	struct	workqueue_worker (*workers)[];
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

#define INIT_WORK(work, _func) 		 	\
do {						\
	(work)->ws_entries.stqe_next = NULL;	\
	(work)->func = (_func);			\
	(work)->on_queue = false;		\
	(work)->running = false;		\
	(work)->canceled = false;		\
} while (0)

#define INIT_WORK_ONSTACK(work, _func)	INIT_WORK(work, _func)

#define INIT_DELAYED_WORK(_work, _func)					\
do {									\
	INIT_WORK(&(_work)->work, _func);				\
	callout_init_mp(&(_work)->timer);				\
} while (0)

#define INIT_DELAYED_WORK_ONSTACK(work, _func)	INIT_DELAYED_WORK(work, _func)

/* System-wide workqueues */
extern struct workqueue_struct *system_wq;
extern struct workqueue_struct *system_highpri_wq;
extern struct workqueue_struct *system_long_wq;
extern struct workqueue_struct *system_unbound_wq;
extern struct workqueue_struct *system_power_efficient_wq;

#define alloc_ordered_workqueue(name, flags) \
	_create_workqueue_common(name, (flags) | WQ_UNBOUND)

#define alloc_workqueue(name, flags, max_active) \
	_create_workqueue_common(name, flags)

#define create_singlethread_workqueue(name) \
	_create_workqueue_common(name, WQ_UNBOUND)

struct workqueue_struct *_create_workqueue_common(const char *name, int flags);

int queue_work(struct workqueue_struct *wq, struct work_struct *work);
int queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *work,
    unsigned long delay);

static inline bool
schedule_work(struct work_struct *work)
{
	return queue_work(system_wq, work);
}

static inline bool schedule_delayed_work(struct delayed_work *dwork,
                                         unsigned long delay)
{
        return queue_delayed_work(system_wq, dwork, delay);
}

bool cancel_work_sync(struct work_struct *work);
bool cancel_delayed_work(struct delayed_work *dwork);
bool cancel_delayed_work_sync(struct delayed_work *dwork);

/* XXX: Return value not used in drm code */
static inline bool
mod_delayed_work(struct workqueue_struct *wq,
		 struct delayed_work *dwork, unsigned long delay)
{
	cancel_delayed_work(dwork);
	queue_delayed_work(wq, dwork, delay);
	return false;
}

void drain_workqueue(struct workqueue_struct *wq);
void flush_workqueue(struct workqueue_struct *wq);

bool flush_work(struct work_struct *work);
bool flush_delayed_work(struct delayed_work *dwork);

static inline void
flush_scheduled_work(void)
{
	flush_workqueue(system_wq);
}

unsigned int work_busy(struct work_struct *work);
bool work_pending(struct work_struct *work);

bool delayed_work_pending(struct delayed_work *dw);

void destroy_workqueue(struct workqueue_struct *wq);

void destroy_work_on_stack(struct work_struct *work);

void destroy_delayed_work_on_stack(struct delayed_work *work);

#endif	/* _LINUX_WORKQUEUE_H_ */
