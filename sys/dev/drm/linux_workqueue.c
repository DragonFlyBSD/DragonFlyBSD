/*
 * Copyright (c) 2015-2020 François Tigeot <ftigeot@wolfpond.org>
 * Copyright (c) 2020 Matthew Dillon <dillon@backplane.com>
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

#include <drm/drmP.h>
#include <linux/workqueue.h>

#include <sys/kthread.h>

/*
   Running behaviour, from kernel.org docs:
   - While there are work items on the workqueue the worker executes the functions
   associated with the work items one after the other.
   - When there is no work item left on the workqueue the worker becomes idle.

   There are two worker-pools,
   one for normal work items
   and the other for high priority ones, for each possible CPU
   and some extra worker-pools to serve work items queued on unbound workqueues
   - the number of these backing pools is dynamic.
 */

/* XXX: Linux functions often enable/disable irqs on the CPU they run on
 * this should be investigated */

struct workqueue_struct *system_wq;
struct workqueue_struct *system_highpri_wq;
struct workqueue_struct *system_long_wq;
struct workqueue_struct *system_unbound_wq;
struct workqueue_struct *system_power_efficient_wq;

/*
 * Linux now uses these worker pools:
 * - (per cpu) regular
 * - (per cpu) regular high priority
 * - ordered
 * - ordered high priority
 * - unbound
 * - unbound high priority
 */

static inline void
process_all_work(struct workqueue_worker *worker)
{
	struct work_struct *work;
	bool didcan;

	while (STAILQ_FIRST(&worker->ws_list_head)) {
		work = STAILQ_FIRST(&worker->ws_list_head);
		STAILQ_REMOVE_HEAD(&worker->ws_list_head, ws_entries);
		work->on_queue = false;

		/* A work shouldn't be executed concurrently on a single cpu */
		if (work->running)
			continue;

		/* Do not run canceled works */
		if (work->canceled) {
			/* XXX: should we allow canceled works to be reenabled ? */
			work->canceled = false;
			continue;
		}

		work->running = true;
		lockmgr(&worker->worker_lock, LK_RELEASE);
		work->func(work);
		lwkt_yield();
		lockmgr(&worker->worker_lock, LK_EXCLUSIVE);
		if (work->on_queue == false)
			work->worker = NULL;
		didcan = work->canceled;
		cpu_sfence();
		work->running = false;
		if (didcan == true)
			wakeup(work);
	}
}

static void
wq_worker_thread(void *arg)
{
	struct workqueue_worker *worker = arg;

	lockmgr(&worker->worker_lock, LK_EXCLUSIVE);
	while (1) {
		process_all_work(worker);
		lksleep(worker, &worker->worker_lock, 0, "wqidle", 0);
	}
	lockmgr(&worker->worker_lock, LK_RELEASE);
}

/*
 * Return false if work was already on a queue
 * Return true and queue it if this was not the case
 */
int
queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	struct workqueue_worker *worker;
	int ret = false;

	/* XXX: should we block instead ? */
	if (wq->is_draining)
		return false;

	if (wq->num_workers > 1)
		worker = &(*wq->workers)[mycpuid];
	else
		worker = &(*wq->workers)[0];

	lockmgr(&worker->worker_lock, LK_EXCLUSIVE);
	work->canceled = false;
	if (work->on_queue == false || work->running == false) {
		if (work->on_queue == false) {
			STAILQ_INSERT_TAIL(&worker->ws_list_head, work,
					   ws_entries);
			work->on_queue = true;
			work->worker = worker;
			wakeup_one(worker);
		}
		ret = true;
	}
	lockmgr(&worker->worker_lock, LK_RELEASE);

	return ret;
}

static inline void
_delayed_work_fn(void *arg)
{
	struct delayed_work *dw = arg;

	queue_work(system_wq, &dw->work);
}

int
queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *work,
    unsigned long delay)
{
	int pending = work->work.on_queue; // XXX: running too ?
	if (delay != 0) {
		callout_reset(&work->timer, delay, _delayed_work_fn, work);
	} else {
		_delayed_work_fn((void *)work);
	}

	return (!pending);
}

static int
init_workqueues(void *arg)
{
	system_wq = alloc_workqueue("system_wq", 0, 1);
	system_highpri_wq = alloc_workqueue("system_highpri_wq", WQ_HIGHPRI, 1);
	system_long_wq = alloc_workqueue("system_long_wq", 0, 1);
	system_unbound_wq = alloc_workqueue("system_unbound_wq", WQ_UNBOUND, 1);
	system_power_efficient_wq = alloc_workqueue("system_power_efficient_wq", 0, 1);

	return 0;
}

static int destroy_workqueues(void *arg)
{
	destroy_workqueue(system_wq);
	destroy_workqueue(system_highpri_wq);
	destroy_workqueue(system_long_wq);
	destroy_workqueue(system_unbound_wq);
	destroy_workqueue(system_power_efficient_wq);

	return 0;
}

struct workqueue_struct *
_create_workqueue_common(const char *name, int flags)
{
	struct workqueue_struct *wq;
	int priority, error;

	wq = kmalloc(sizeof(*wq), M_DRM, M_WAITOK | M_ZERO);

	if (flags & WQ_HIGHPRI)
		priority = TDPRI_INT_SUPPORT;
	else
		priority = TDPRI_KERN_DAEMON;

	if (flags & WQ_UNBOUND) {
		wq->num_workers = 1;
	} else {
		wq->num_workers = ncpus;
	}
	wq->workers = kmalloc(sizeof(struct workqueue_worker) * wq->num_workers,
			M_DRM, M_WAITOK | M_ZERO);

	for (int i = 0;i < wq->num_workers; i++) {
		struct workqueue_worker *worker = &(*wq->workers)[i];

		lockinit(&worker->worker_lock, "lwq", 0, 0);
		STAILQ_INIT(&worker->ws_list_head);
		if (wq->num_workers > 1) {
			error = lwkt_create(wq_worker_thread, worker,
				    &worker->worker_thread, NULL, TDF_NOSTART, i, "%s/%d", name, i);
		} else {
			error = lwkt_create(wq_worker_thread, worker,
				    &worker->worker_thread, NULL, TDF_NOSTART, -1, name);
		}
		if (error) {
			kprintf("%s: lwkt_create(%s/%d): error %d",
			    __func__, name, i, error);
			/* XXX: destroy kernel threads and free workers[] if applicable */
			kfree(wq);
			return NULL;
		}
		lwkt_setpri_initial(worker->worker_thread, priority);
		lwkt_schedule(worker->worker_thread);
	}

	return wq;
}

void
destroy_workqueue(struct workqueue_struct *wq)
{
	drain_workqueue(wq);
//	wq->is_draining = true;
#if 0	/* XXX TODO */
	kill_all_threads;
	kfree(wq->wq_threads);
	kfree(wq);
#endif
}

SYSINIT(linux_workqueue_init, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, init_workqueues, NULL);
SYSUNINIT(linux_workqueue_destroy, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, destroy_workqueues, NULL);

bool
flush_delayed_work(struct delayed_work *dwork)
{
	callout_drain(&dwork->timer);
	return flush_work(&dwork->work);
}

/* Wait until the wq becomes empty */
void
drain_workqueue(struct workqueue_struct *wq)
{
	struct workqueue_worker *worker;

	wq->is_draining = true;

	for (int i=0;i < wq->num_workers; i++) {
		worker = &(*wq->workers)[i];

		lockmgr(&worker->worker_lock, LK_EXCLUSIVE);
		while (!STAILQ_EMPTY(&worker->ws_list_head)) {
		/* XXX: introduces latency */
			tsleep(&drain_workqueue, 0, "wkdrain", 1);
		}
		lockmgr(&worker->worker_lock, LK_RELEASE);
	}

	/* XXX: No more work will be queued. is that right ? */
//	wq->is_draining = false;
}

bool
work_pending(struct work_struct *work)
{
	/* XXX: is on_queue the only constraint ? */
	return work->on_queue;
}

unsigned int
work_busy(struct work_struct *work)
{
	return (work->on_queue || work->running);
}

static inline void
__flush_work_func(struct work_struct *work)
{
	wakeup_one(work);
}

/* XXX introduces latency ? */
void
flush_workqueue(struct workqueue_struct *wq)
{
	struct work_struct __flush_work;

	INIT_WORK(&__flush_work, __flush_work_func);

	queue_work(wq, &__flush_work);
	while (__flush_work.on_queue || __flush_work.running) {
		tsleep(&__flush_work, 0, "flshwq", 0);
	}
}

/*
 * Wait until a work is done (has been executed)
 * Return true if this function had to wait, and false otherwise
 */
bool
flush_work(struct work_struct *work)
{
	int ret = false;

	/* XXX: probably unreliable */
	while (work->on_queue || work->running) {
		ret = true;
		/* XXX: use something more intelligent than tsleep() */
		tsleep(&flush_work, 0, "flshwrk", 1);
	}

	return ret;
}

static inline bool
_cancel_work(struct work_struct *work, bool sync_wait)
{
	struct workqueue_worker *worker;
	bool ret;

	ret = false;

	for (;;) {
		if (work->on_queue) {
			worker = work->worker;
			if (worker == NULL)
				continue;
			lockmgr(&worker->worker_lock, LK_EXCLUSIVE);
			if (worker != work->worker || work->on_queue == false) {
				lockmgr(&worker->worker_lock, LK_RELEASE);
				continue;
			}
			STAILQ_REMOVE(&worker->ws_list_head, work,
				      work_struct, ws_entries);
			work->on_queue = false;
			ret = true;
			lockmgr(&worker->worker_lock, LK_RELEASE);
		}
		if (work->running == false)
			break;

		worker = work->worker;
		if (worker == NULL)
			continue;
		lockmgr(&worker->worker_lock, LK_EXCLUSIVE);
		if (worker != work->worker || work->running == false) {
			lockmgr(&worker->worker_lock, LK_RELEASE);
			continue;
		}
		work->canceled = true;
		ret = true;
		if (sync_wait == false) {
			lockmgr(&worker->worker_lock, LK_RELEASE);
			break;
		}
		/* XXX this races */
		lksleep(work, &worker->worker_lock, 0, "wqcan", 1);
		lockmgr(&worker->worker_lock, LK_RELEASE);
		/* retest */
	}

	return ret;
}

/*
 * If work was queued, remove it from the queue and return true.
 * If work was not queued, return false.
 * In any case, wait for work to complete or be removed from the workqueue,
 * callers may free associated data structures after this call.
 */
bool
cancel_work_sync(struct work_struct *work)
{
	return _cancel_work(work, true);
}

/* Return false if work wasn't pending
 * Return true if work was pending and canceled */
bool
cancel_delayed_work(struct delayed_work *dwork)
{
	struct work_struct *work = &dwork->work;

	work->canceled = true;
	callout_cancel(&dwork->timer);

	return _cancel_work(work, false);
}

bool
cancel_delayed_work_sync(struct delayed_work *dwork)
{
	struct work_struct *work = &dwork->work;

	work->canceled = true;
	callout_cancel(&dwork->timer);

	return _cancel_work(work, true);
}

bool
delayed_work_pending(struct delayed_work *dw)
{
	/* XXX: possibly wrong if the timer hasn't yet fired */
	return work_pending(&dw->work);
}

void
destroy_work_on_stack(struct work_struct *work)
{
}

void
destroy_delayed_work_on_stack(struct delayed_work *work)
{
}
