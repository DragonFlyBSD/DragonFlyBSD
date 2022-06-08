/*-
 * Copyright (c) 2000 Doug Rabson
 * Copyright (c) 2014 Jeff Roberson
 * Copyright (c) 2016 Matthew Macy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpumask.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/gtaskqueue.h>
#include <sys/unistd.h>
#include <machine/stdarg.h>

static MALLOC_DEFINE(M_GTASKQUEUE, "gtaskqueue", "Group Task Queues");
static void	gtaskqueue_thread_enqueue(void *);
static void	gtaskqueue_thread_loop(void *arg);
static int	task_is_running(struct gtaskqueue *queue, struct gtask *gtask);
static void	gtaskqueue_drain_locked(struct gtaskqueue *queue, struct gtask *gtask);

TASKQGROUP_DEFINE(softirq, ncpus, 1);

struct gtaskqueue_busy {
	struct gtask		*tb_running;
	u_int			 tb_seq;
	LIST_ENTRY(gtaskqueue_busy) tb_link;
};

typedef void (*gtaskqueue_enqueue_fn)(void *context);

struct gtaskqueue {
	STAILQ_HEAD(, gtask)	tq_queue;
	LIST_HEAD(, gtaskqueue_busy) tq_active;
	u_int			tq_seq;
	int			tq_callouts;
	struct lock		tq_lock;
	gtaskqueue_enqueue_fn	tq_enqueue;
	void			*tq_context;
	const char		*tq_name;
	struct thread		**tq_threads;
	int			tq_tcount;
	int			tq_flags;
#if 0
	taskqueue_callback_fn	tq_callbacks[TASKQUEUE_NUM_CALLBACKS];
	void			*tq_cb_contexts[TASKQUEUE_NUM_CALLBACKS];
#endif
};

#define	TQ_FLAGS_ACTIVE		(1 << 0)
#define	TQ_FLAGS_BLOCKED	(1 << 1)
#define	TQ_FLAGS_UNLOCKED_ENQUEUE	(1 << 2)

#define	DT_CALLOUT_ARMED	(1 << 0)

#define	TQ_LOCK(tq)		lockmgr(&(tq)->tq_lock, LK_EXCLUSIVE)
#define	TQ_ASSERT_LOCKED(tq)	KKASSERT(lockstatus(&(tq)->tq_lock, NULL) != 0)
#define	TQ_UNLOCK(tq)		lockmgr(&(tq)->tq_lock, LK_RELEASE);
#define	TQ_ASSERT_UNLOCKED(tq)	KKASSERT(lockstatus(&(tq)->tq_lock) == 0)

#ifdef INVARIANTS
static void
gtask_dump(struct gtask *gtask)
{
	kprintf("gtask: %p ta_flags=%x ta_priority=%d ta_func=%p "
		"ta_context=%p\n",
		gtask, gtask->ta_flags, gtask->ta_priority,
		gtask->ta_func, gtask->ta_context);
}
#endif

static __inline int
TQ_SLEEP(struct gtaskqueue *tq, void *p, const char *wm)
{
	return (lksleep(p, &tq->tq_lock, 0, wm, 0));
}

static struct gtaskqueue *
_gtaskqueue_create(const char *name, int mflags,
		 taskqueue_enqueue_fn enqueue, void *context,
		 int lkflags, const char *mtxname __unused)
{
	struct gtaskqueue *queue;

	queue = kmalloc(sizeof(struct gtaskqueue),
			M_GTASKQUEUE, mflags | M_ZERO);
	if (!queue) {
		kprintf("_gtaskqueue_create: kmalloc failed %08x\n", mflags);
		return (NULL);
	}

	STAILQ_INIT(&queue->tq_queue);
	LIST_INIT(&queue->tq_active);
	queue->tq_enqueue = enqueue;
	queue->tq_context = context;
	queue->tq_name = name ? name : "taskqueue";
	queue->tq_flags |= TQ_FLAGS_ACTIVE;
	if (enqueue == gtaskqueue_thread_enqueue)
		queue->tq_flags |= TQ_FLAGS_UNLOCKED_ENQUEUE;
	lockinit(&queue->tq_lock, queue->tq_name, 0, 0);

	return (queue);
}

/*
 * Signal a taskqueue thread to terminate.
 */
static void
gtaskqueue_terminate(struct thread **pp, struct gtaskqueue *tq)
{

	while (tq->tq_tcount > 0 || tq->tq_callouts > 0) {
		wakeup(tq);
		TQ_SLEEP(tq, pp, "gtq_destroy");
	}
}

static void __unused
gtaskqueue_free(struct gtaskqueue *queue)
{

	TQ_LOCK(queue);
	queue->tq_flags &= ~TQ_FLAGS_ACTIVE;
	gtaskqueue_terminate(queue->tq_threads, queue);
	KASSERT(LIST_EMPTY(&queue->tq_active), ("Tasks still running?"));
	KASSERT(queue->tq_callouts == 0, ("Armed timeout tasks"));
	lockuninit(&queue->tq_lock);
	kfree(queue->tq_threads, M_GTASKQUEUE);
	/*kfree(queue->tq_name, M_GTASKQUEUE);*/
	kfree(queue, M_GTASKQUEUE);
}

/*
 * Wait for all to complete, then prevent it from being enqueued
 */
void
grouptask_block(struct grouptask *grouptask)
{
	struct gtaskqueue *queue = grouptask->gt_taskqueue;
	struct gtask *gtask = &grouptask->gt_task;

#ifdef INVARIANTS
	if (queue == NULL) {
		gtask_dump(gtask);
		panic("queue == NULL");
	}
#endif
	TQ_LOCK(queue);
	gtask->ta_flags |= TASK_NOENQUEUE;
	gtaskqueue_drain_locked(queue, gtask);
	TQ_UNLOCK(queue);
}

void
grouptask_unblock(struct grouptask *grouptask)
{
	struct gtaskqueue *queue = grouptask->gt_taskqueue;
	struct gtask *gtask = &grouptask->gt_task;

#ifdef INVARIANTS
	if (queue == NULL) {
		gtask_dump(gtask);
		panic("queue == NULL");
	}
#endif
	TQ_LOCK(queue);
	gtask->ta_flags &= ~TASK_NOENQUEUE;
	TQ_UNLOCK(queue);
}

int
grouptaskqueue_enqueue(struct gtaskqueue *queue, struct gtask *gtask)
{
#ifdef INVARIANTS
	if (queue == NULL) {
		gtask_dump(gtask);
		panic("queue == NULL");
	}
#endif
	TQ_LOCK(queue);
	if (gtask->ta_flags & TASK_ENQUEUED) {
		TQ_UNLOCK(queue);
		return (0);
	}
	if (gtask->ta_flags & TASK_NOENQUEUE) {
		TQ_UNLOCK(queue);
		return (EAGAIN);
	}
	STAILQ_INSERT_TAIL(&queue->tq_queue, gtask, ta_link);
	gtask->ta_flags |= TASK_ENQUEUED;
	TQ_UNLOCK(queue);
	if ((queue->tq_flags & TQ_FLAGS_BLOCKED) == 0)
		queue->tq_enqueue(queue->tq_context);
	return (0);
}

static void
gtaskqueue_task_nop_fn(void *context)
{
}

/*
 * Block until all currently queued tasks in this taskqueue
 * have begun execution.  Tasks queued during execution of
 * this function are ignored.
 */
static void
gtaskqueue_drain_tq_queue(struct gtaskqueue *queue)
{
	struct gtask t_barrier;

	if (STAILQ_EMPTY(&queue->tq_queue))
		return;

	/*
	 * Enqueue our barrier after all current tasks, but with
	 * the highest priority so that newly queued tasks cannot
	 * pass it.  Because of the high priority, we can not use
	 * taskqueue_enqueue_locked directly (which drops the lock
	 * anyway) so just insert it at tail while we have the
	 * queue lock.
	 */
	GTASK_INIT(&t_barrier, 0, USHRT_MAX, gtaskqueue_task_nop_fn, &t_barrier);
	STAILQ_INSERT_TAIL(&queue->tq_queue, &t_barrier, ta_link);
	t_barrier.ta_flags |= TASK_ENQUEUED;

	/*
	 * Once the barrier has executed, all previously queued tasks
	 * have completed or are currently executing.
	 */
	while (t_barrier.ta_flags & TASK_ENQUEUED)
		TQ_SLEEP(queue, &t_barrier, "gtq_qdrain");
}

/*
 * Block until all currently executing tasks for this taskqueue
 * complete.  Tasks that begin execution during the execution
 * of this function are ignored.
 */
static void
gtaskqueue_drain_tq_active(struct gtaskqueue *queue)
{
	struct gtaskqueue_busy *tb;
	u_int seq;

	if (LIST_EMPTY(&queue->tq_active))
		return;

	/* Block taskq_terminate().*/
	queue->tq_callouts++;

	/* Wait for any active task with sequence from the past. */
	seq = queue->tq_seq;
restart:
	LIST_FOREACH(tb, &queue->tq_active, tb_link) {
		if ((int)(tb->tb_seq - seq) <= 0) {
			TQ_SLEEP(queue, tb->tb_running, "gtq_adrain");
			goto restart;
		}
	}

	/* Release taskqueue_terminate(). */
	queue->tq_callouts--;
	if ((queue->tq_flags & TQ_FLAGS_ACTIVE) == 0)
		wakeup_one(queue->tq_threads);
}

void
gtaskqueue_block(struct gtaskqueue *queue)
{

	TQ_LOCK(queue);
	queue->tq_flags |= TQ_FLAGS_BLOCKED;
	TQ_UNLOCK(queue);
}

void
gtaskqueue_unblock(struct gtaskqueue *queue)
{

	TQ_LOCK(queue);
	queue->tq_flags &= ~TQ_FLAGS_BLOCKED;
	if (!STAILQ_EMPTY(&queue->tq_queue))
		queue->tq_enqueue(queue->tq_context);
	TQ_UNLOCK(queue);
}

static void
gtaskqueue_run_locked(struct gtaskqueue *queue)
{
	struct gtaskqueue_busy tb;
	struct gtask *gtask;
#if 0
	struct epoch_tracker et;
	bool in_net_epoch;
#endif

	KASSERT(queue != NULL, ("tq is NULL"));
	TQ_ASSERT_LOCKED(queue);
	tb.tb_running = NULL;
	LIST_INSERT_HEAD(&queue->tq_active, &tb, tb_link);
#if 0
	in_net_epoch = false;
#endif

	while ((gtask = STAILQ_FIRST(&queue->tq_queue)) != NULL) {
		STAILQ_REMOVE_HEAD(&queue->tq_queue, ta_link);
		gtask->ta_flags &= ~TASK_ENQUEUED;
		tb.tb_running = gtask;
		tb.tb_seq = ++queue->tq_seq;
		TQ_UNLOCK(queue);

		KASSERT(gtask->ta_func != NULL, ("task->ta_func is NULL"));
#if 0
		if (!in_net_epoch && TASK_IS_NET(gtask)) {
			in_net_epoch = true;
			NET_EPOCH_ENTER(et);
		} else if (in_net_epoch && !TASK_IS_NET(gtask)) {
			NET_EPOCH_EXIT(et);
			in_net_epoch = false;
		}
#endif
		gtask->ta_func(gtask->ta_context);

		TQ_LOCK(queue);
		wakeup(gtask);
	}
#if 0
	if (in_net_epoch)
		NET_EPOCH_EXIT(et);
#endif
	LIST_REMOVE(&tb, tb_link);
}

static int
task_is_running(struct gtaskqueue *queue, struct gtask *gtask)
{
	struct gtaskqueue_busy *tb;

	TQ_ASSERT_LOCKED(queue);
	LIST_FOREACH(tb, &queue->tq_active, tb_link) {
		if (tb->tb_running == gtask)
			return (1);
	}
	return (0);
}

static int
gtaskqueue_cancel_locked(struct gtaskqueue *queue, struct gtask *gtask)
{

	if (gtask->ta_flags & TASK_ENQUEUED)
		STAILQ_REMOVE(&queue->tq_queue, gtask, gtask, ta_link);
	gtask->ta_flags &= ~TASK_ENQUEUED;
	return (task_is_running(queue, gtask) ? EBUSY : 0);
}

int
gtaskqueue_cancel(struct gtaskqueue *queue, struct gtask *gtask)
{
	int error;

	TQ_LOCK(queue);
	error = gtaskqueue_cancel_locked(queue, gtask);
	TQ_UNLOCK(queue);

	return (error);
}

static void
gtaskqueue_drain_locked(struct gtaskqueue *queue, struct gtask *gtask)
{
	while ((gtask->ta_flags & TASK_ENQUEUED) || task_is_running(queue, gtask))
		TQ_SLEEP(queue, gtask, "gtq_drain");
}

void
gtaskqueue_drain(struct gtaskqueue *queue, struct gtask *gtask)
{
	TQ_LOCK(queue);
	gtaskqueue_drain_locked(queue, gtask);
	TQ_UNLOCK(queue);
}

void
gtaskqueue_drain_all(struct gtaskqueue *queue)
{

	TQ_LOCK(queue);
	gtaskqueue_drain_tq_queue(queue);
	gtaskqueue_drain_tq_active(queue);
	TQ_UNLOCK(queue);
}

static int __printflike(4, 0)
_gtaskqueue_start_threads(struct gtaskqueue **tqp, int count, int pri,
			  const char *name, __va_list ap)
{
	char ktname[MAXCOMLEN + 1];
	struct thread *td;
	struct gtaskqueue *tq;
	int i, error;

	if (count <= 0)
		return (EINVAL);

	kvsnprintf(ktname, sizeof(ktname), name, ap);
	tq = *tqp;

	tq->tq_threads = kmalloc(sizeof(struct thread *) * count,
				 M_GTASKQUEUE, M_WAITOK | M_ZERO);

	for (i = 0; i < count; i++) {
		int cpu = i % ncpus;
		if (count == 1) {
			error = lwkt_create(gtaskqueue_thread_loop, tqp,
					    &tq->tq_threads[i], NULL,
					    TDF_NOSTART, cpu,
					    "%s", ktname);
		} else {
			error = lwkt_create(gtaskqueue_thread_loop, tqp,
					    &tq->tq_threads[i], NULL,
					    TDF_NOSTART, cpu,
					    "%s_%d", ktname, i);
		}
		if (error) {
			/* should be ok to continue, taskqueue_free will dtrt */
			kprintf("%s: lwkt_create(%s): error %d",
				__func__, ktname, error);
			tq->tq_threads[i] = NULL;		/* paranoid */
		} else
			tq->tq_tcount++;
	}
	for (i = 0; i < count; i++) {
		if (tq->tq_threads[i] == NULL)
			continue;
		td = tq->tq_threads[i];
		lwkt_setpri_initial(td, pri);
		lwkt_schedule(td);
	}

	return (0);
}

static int __printflike(4, 5)
gtaskqueue_start_threads(struct gtaskqueue **tqp, int count, int pri,
			 const char *name, ...)
{
	__va_list ap;
	int error;

	__va_start(ap, name);
	error = _gtaskqueue_start_threads(tqp, count, pri, name, ap);
	__va_end(ap);
	return (error);
}

#if 0
static inline void
gtaskqueue_run_callback(struct gtaskqueue *tq,
    enum taskqueue_callback_type cb_type)
{
	taskqueue_callback_fn tq_callback;

	TQ_ASSERT_UNLOCKED(tq);
	tq_callback = tq->tq_callbacks[cb_type];
	if (tq_callback != NULL)
		tq_callback(tq->tq_cb_contexts[cb_type]);
}
#endif

static void
gtaskqueue_thread_loop(void *arg)
{
	struct gtaskqueue **tqp, *tq;

	tqp = arg;
	tq = *tqp;
#if 0
	gtaskqueue_run_callback(tq, TASKQUEUE_CALLBACK_TYPE_INIT);
#endif
	TQ_LOCK(tq);
	while ((tq->tq_flags & TQ_FLAGS_ACTIVE) != 0) {
		/* XXX ? */
		gtaskqueue_run_locked(tq);
		/*
		 * Because taskqueue_run() can drop tq_mutex, we need to
		 * check if the TQ_FLAGS_ACTIVE flag wasn't removed in the
		 * meantime, which means we missed a wakeup.
		 */
		if ((tq->tq_flags & TQ_FLAGS_ACTIVE) == 0)
			break;
		TQ_SLEEP(tq, tq, "-");
	}
	gtaskqueue_run_locked(tq);
	/*
	 * This thread is on its way out, so just drop the lock temporarily
	 * in order to call the shutdown callback.  This allows the callback
	 * to look at the taskqueue, even just before it dies.
	 */
#if 0
	TQ_UNLOCK(tq);
	gtaskqueue_run_callback(tq, TASKQUEUE_CALLBACK_TYPE_SHUTDOWN);
	TQ_LOCK(tq);
#endif

	/* rendezvous with thread that asked us to terminate */
	tq->tq_tcount--;
	wakeup_one(tq->tq_threads);
	TQ_UNLOCK(tq);
	lwkt_exit();
}

static void
gtaskqueue_thread_enqueue(void *context)
{
	struct gtaskqueue **tqp, *tq;

	tqp = context;
	tq = *tqp;
	wakeup_one(tq);
}

/*
 * NOTE: FreeBSD uses MTX_SPIN locks, which doesn't make a whole lot
 *	 of sense (over-use of spin-locks in general). In DFly we
 *	 want to use blockable locks for almost everything.
 */
static struct gtaskqueue *
gtaskqueue_create_fast(const char *name, int mflags,
		       taskqueue_enqueue_fn enqueue, void *context)
{
	return _gtaskqueue_create(name, mflags, enqueue, context,
				  0, "fast_taskqueue");
}

struct taskqgroup_cpu {
	LIST_HEAD(, grouptask) tgc_tasks;
	struct gtaskqueue *tgc_taskq;
	int		tgc_cnt;
	int		tgc_cpu;
};

struct taskqgroup {
	struct taskqgroup_cpu tqg_queue[MAXCPU];
	struct lock	tqg_lock;
	const char *	tqg_name;
	int		tqg_cnt;
};

struct taskq_bind_task {
	struct gtask bt_task;
	int	bt_cpuid;
};

static void
taskqgroup_cpu_create(struct taskqgroup *qgroup, int idx, int cpu)
{
	struct taskqgroup_cpu *qcpu;

	qcpu = &qgroup->tqg_queue[idx];
	LIST_INIT(&qcpu->tgc_tasks);
	qcpu->tgc_taskq = gtaskqueue_create_fast(NULL, M_WAITOK,
						 gtaskqueue_thread_enqueue,
						 &qcpu->tgc_taskq);
	gtaskqueue_start_threads(&qcpu->tgc_taskq, 1, TDPRI_KERN_DAEMON,
				 "%s_%d", qgroup->tqg_name, idx);
	qcpu->tgc_cpu = cpu;
}

/*
 * Find the taskq with least # of tasks that doesn't currently have any
 * other queues from the uniq identifier.
 */
static int
taskqgroup_find(struct taskqgroup *qgroup, void *uniq)
{
	struct grouptask *n;
	int i, idx, mincnt;
	int strict;

	KKASSERT(lockstatus(&qgroup->tqg_lock, NULL) != 0);
	KASSERT(qgroup->tqg_cnt != 0,
	    ("qgroup %s has no queues", qgroup->tqg_name));

	/*
	 * Two passes: first scan for a queue with the least tasks that
	 * does not already service this uniq id.  If that fails simply find
	 * the queue with the least total tasks.
	 */
	for (idx = -1, mincnt = INT_MAX, strict = 1; mincnt == INT_MAX;
	    strict = 0) {
		for (i = 0; i < qgroup->tqg_cnt; i++) {
			if (qgroup->tqg_queue[i].tgc_cnt > mincnt)
				continue;
			if (strict) {
				LIST_FOREACH(n, &qgroup->tqg_queue[i].tgc_tasks,
				    gt_list)
					if (n->gt_uniq == uniq)
						break;
				if (n != NULL)
					continue;
			}
			mincnt = qgroup->tqg_queue[i].tgc_cnt;
			idx = i;
		}
	}
	if (idx == -1)
		panic("%s: failed to pick a qid.", __func__);

	return (idx);
}

void
taskqgroup_attach(struct taskqgroup *qgroup, struct grouptask *gtask,
    void *uniq, device_t dev, struct resource *irq, const char *name)
{
	int cpu, qid, error;

	KASSERT(qgroup->tqg_cnt > 0,
	    ("qgroup %s has no queues", qgroup->tqg_name));

	gtask->gt_uniq = uniq;
	ksnprintf(gtask->gt_name, GROUPTASK_NAMELEN, "%s", name ? name : "grouptask");
	gtask->gt_dev = dev;
	gtask->gt_irq = irq;
	gtask->gt_cpu = -1;
	lockmgr(&qgroup->tqg_lock, LK_EXCLUSIVE);
	qid = taskqgroup_find(qgroup, uniq);
	qgroup->tqg_queue[qid].tgc_cnt++;
	LIST_INSERT_HEAD(&qgroup->tqg_queue[qid].tgc_tasks, gtask, gt_list);
	gtask->gt_taskqueue = qgroup->tqg_queue[qid].tgc_taskq;
	if (dev != NULL && irq != NULL) {
		cpu = qgroup->tqg_queue[qid].tgc_cpu;
		gtask->gt_cpu = cpu;
		lockmgr(&qgroup->tqg_lock, LK_RELEASE);
#if 0
		/*
		 * XXX FreeBSD created a mess by separating out the cpu
		 * binding from bus_setup_intr().  Punt for now.
		 */
		error = bus_bind_intr(dev, irq, cpu);
#endif
		error = 0;

		if (error)
			kprintf("%s: binding interrupt failed for %s: %d\n",
			    __func__, gtask->gt_name, error);
	} else {
		lockmgr(&qgroup->tqg_lock, LK_RELEASE);
	}
}

int
taskqgroup_attach_cpu(struct taskqgroup *qgroup, struct grouptask *gtask,
    void *uniq, int cpu, device_t dev, struct resource *irq, const char *name)
{
	int i, qid, error;

	gtask->gt_uniq = uniq;
	ksnprintf(gtask->gt_name, GROUPTASK_NAMELEN, "%s", name ? name : "grouptask");
	gtask->gt_dev = dev;
	gtask->gt_irq = irq;
	gtask->gt_cpu = cpu;
	lockmgr(&qgroup->tqg_lock, LK_EXCLUSIVE);
	for (i = 0, qid = -1; i < qgroup->tqg_cnt; i++) {
		if (qgroup->tqg_queue[i].tgc_cpu == cpu) {
			qid = i;
			break;
		}
	}
	if (qid == -1) {
		lockmgr(&qgroup->tqg_lock, LK_RELEASE);
		kprintf("%s: qid not found for %s cpu=%d\n",
			__func__, gtask->gt_name, cpu);
		return (EINVAL);
	}
	qgroup->tqg_queue[qid].tgc_cnt++;
	LIST_INSERT_HEAD(&qgroup->tqg_queue[qid].tgc_tasks, gtask, gt_list);
	gtask->gt_taskqueue = qgroup->tqg_queue[qid].tgc_taskq;
	cpu = qgroup->tqg_queue[qid].tgc_cpu;
	lockmgr(&qgroup->tqg_lock, LK_RELEASE);

	if (dev != NULL && irq != NULL) {
#if 0
		/*
		 * XXX FreeBSD created a mess by separating out the cpu
		 * binding from bus_setup_intr().  Punt for now.
		 */
		error = bus_bind_intr(dev, irq, cpu);
#endif
		error = 0;

		if (error) {
			kprintf("%s: binding interrupt failed for %s: %d\n",
			    __func__, gtask->gt_name, error);
		}
	}
	return (0);
}

void
taskqgroup_detach(struct taskqgroup *qgroup, struct grouptask *gtask)
{
	int i;

	grouptask_block(gtask);
	lockmgr(&qgroup->tqg_lock, LK_EXCLUSIVE);
	for (i = 0; i < qgroup->tqg_cnt; i++)
		if (qgroup->tqg_queue[i].tgc_taskq == gtask->gt_taskqueue)
			break;
	if (i == qgroup->tqg_cnt)
		panic("%s: task %s not in group", __func__, gtask->gt_name);
	qgroup->tqg_queue[i].tgc_cnt--;
	LIST_REMOVE(gtask, gt_list);
	lockmgr(&qgroup->tqg_lock, LK_RELEASE);
	gtask->gt_taskqueue = NULL;
	gtask->gt_task.ta_flags &= ~TASK_NOENQUEUE;
}

static void
taskqgroup_binder(void *ctx)
{
	struct taskq_bind_task *gtask;

	gtask = ctx;
	lwkt_migratecpu(gtask->bt_cpuid);
	kfree(gtask, M_DEVBUF);
}

void
taskqgroup_bind(struct taskqgroup *qgroup)
{
	struct taskq_bind_task *gtask;
	int i;

	/*
	 * Bind taskqueue threads to specific CPUs, if they have been assigned
	 * one.
	 */
	if (qgroup->tqg_cnt == 1)
		return;

	for (i = 0; i < qgroup->tqg_cnt; i++) {
		gtask = kmalloc(sizeof(*gtask), M_DEVBUF, M_WAITOK);
		GTASK_INIT(&gtask->bt_task, 0, 0, taskqgroup_binder, gtask);
		gtask->bt_cpuid = qgroup->tqg_queue[i].tgc_cpu;
		grouptaskqueue_enqueue(qgroup->tqg_queue[i].tgc_taskq,
				       &gtask->bt_task);
	}
}

struct taskqgroup *
taskqgroup_create(const char *name, int cnt, int stride)
{
	struct taskqgroup *qgroup;
	int cpu, i, j;

	qgroup = kmalloc(sizeof(*qgroup), M_GTASKQUEUE, M_WAITOK | M_ZERO);
	lockinit(&qgroup->tqg_lock, "taskqgroup", 0, 0);
	qgroup->tqg_name = name;
	qgroup->tqg_cnt = cnt;

	for (cpu = i = 0; i < cnt; i++) {
		taskqgroup_cpu_create(qgroup, i, cpu);
		for (j = 0; j < stride; j++)
			cpu = (cpu + 1) % ncpus;
	}
	return (qgroup);
}

void
taskqgroup_destroy(struct taskqgroup *qgroup)
{
}

void
taskqgroup_drain_all(struct taskqgroup *tqg)
{
	struct gtaskqueue *q;

	for (int i = 0; i < ncpus; i++) {
		q = tqg->tqg_queue[i].tgc_taskq;
		if (q == NULL)
			continue;
		gtaskqueue_drain_all(q);
	}
}
