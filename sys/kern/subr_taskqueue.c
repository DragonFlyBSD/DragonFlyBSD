/*-
 * Copyright (c) 2000 Doug Rabson
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
 *
 *	 $FreeBSD: src/sys/kern/subr_taskqueue.c,v 1.1.2.3 2003/09/10 00:40:39 ken Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/taskqueue.h>
#include <sys/interrupt.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/kthread.h>
#include <sys/thread2.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/serialize.h>
#include <sys/proc.h>
#include <machine/varargs.h>

MALLOC_DEFINE(M_TASKQUEUE, "taskqueue", "Task Queues");

static STAILQ_HEAD(taskqueue_list, taskqueue) taskqueue_queues;
static struct lock	taskqueue_queues_lock;

struct taskqueue {
	STAILQ_ENTRY(taskqueue)	tq_link;
	STAILQ_HEAD(, task)	tq_queue;
	const char		*tq_name;
	taskqueue_enqueue_fn	tq_enqueue;
	void			*tq_context;

	struct task		*tq_running;
	struct spinlock		tq_lock;
	struct thread		**tq_threads;
	int			tq_tcount;
	int			tq_flags;
};

#define	TQ_FLAGS_ACTIVE		(1 << 0)
#define	TQ_FLAGS_BLOCKED	(1 << 1)
#define	TQ_FLAGS_PENDING	(1 << 2)

static void taskqueue_run(struct taskqueue *queue, int lock_held);

static __inline void
TQ_LOCK_INIT(struct taskqueue *tq)
{
	spin_init(&tq->tq_lock);
}

static __inline void
TQ_LOCK_UNINIT(struct taskqueue *tq)
{
	spin_uninit(&tq->tq_lock);
}

static __inline void
TQ_LOCK(struct taskqueue *tq)
{
	spin_lock(&tq->tq_lock);
}

static __inline void
TQ_UNLOCK(struct taskqueue *tq)
{
	spin_unlock(&tq->tq_lock);
}

static __inline void
TQ_SLEEP(struct taskqueue *tq, void *ident, const char *wmesg)
{
	ssleep(ident, &tq->tq_lock, 0, wmesg, 0);
}

struct taskqueue *
taskqueue_create(const char *name, int mflags,
		 taskqueue_enqueue_fn enqueue, void *context)
{
	struct taskqueue *queue;

	queue = kmalloc(sizeof(*queue), M_TASKQUEUE, mflags | M_ZERO);
	if (!queue)
		return NULL;
	STAILQ_INIT(&queue->tq_queue);
	queue->tq_name = name;
	queue->tq_enqueue = enqueue;
	queue->tq_context = context;
	queue->tq_flags |= TQ_FLAGS_ACTIVE;
	TQ_LOCK_INIT(queue);

	lockmgr(&taskqueue_queues_lock, LK_EXCLUSIVE);
	STAILQ_INSERT_TAIL(&taskqueue_queues, queue, tq_link);
	lockmgr(&taskqueue_queues_lock, LK_RELEASE);

	return queue;
}

static void
taskqueue_terminate(struct thread **pp, struct taskqueue *tq)
{
	while(tq->tq_tcount > 0) {
		wakeup(tq);
		TQ_SLEEP(tq, pp, "taskqueue_terminate");
	}
}

void
taskqueue_free(struct taskqueue *queue)
{
	TQ_LOCK(queue);
	queue->tq_flags &= ~TQ_FLAGS_ACTIVE;
	taskqueue_run(queue, 1);
	taskqueue_terminate(queue->tq_threads, queue);
	TQ_UNLOCK(queue);

	lockmgr(&taskqueue_queues_lock, LK_EXCLUSIVE);
	STAILQ_REMOVE(&taskqueue_queues, queue, taskqueue, tq_link);
	lockmgr(&taskqueue_queues_lock, LK_RELEASE);

	TQ_LOCK_UNINIT(queue);

	kfree(queue, M_TASKQUEUE);
}

struct taskqueue *
taskqueue_find(const char *name)
{
	struct taskqueue *queue;

	lockmgr(&taskqueue_queues_lock, LK_EXCLUSIVE);
	STAILQ_FOREACH(queue, &taskqueue_queues, tq_link) {
		if (!strcmp(queue->tq_name, name)) {
			lockmgr(&taskqueue_queues_lock, LK_RELEASE);
			return queue;
		}
	}
	lockmgr(&taskqueue_queues_lock, LK_RELEASE);
	return NULL;
}

/*
 * NOTE!  If using the per-cpu taskqueues ``taskqueue_thread[mycpuid]'',
 * be sure NOT TO SHARE the ``task'' between CPUs.  TASKS ARE NOT LOCKED.
 * So either use a throwaway task which will only be enqueued once, or
 * use one task per CPU!
 */
int
taskqueue_enqueue(struct taskqueue *queue, struct task *task)
{
	struct task *ins;
	struct task *prev;

	TQ_LOCK(queue);

	/*
	 * Don't allow new tasks on a queue which is being freed.
	 */
	if ((queue->tq_flags & TQ_FLAGS_ACTIVE) == 0) {
		TQ_UNLOCK(queue);
		return EPIPE;
	}

	/*
	 * Count multiple enqueues.
	 */
	if (task->ta_pending) {
		task->ta_pending++;
		TQ_UNLOCK(queue);
		return 0;
	}

	/*
	 * Optimise the case when all tasks have the same priority.
	 */
	prev = STAILQ_LAST(&queue->tq_queue, task, ta_link);
	if (!prev || prev->ta_priority >= task->ta_priority) {
		STAILQ_INSERT_TAIL(&queue->tq_queue, task, ta_link);
	} else {
		prev = NULL;
		for (ins = STAILQ_FIRST(&queue->tq_queue); ins;
		     prev = ins, ins = STAILQ_NEXT(ins, ta_link))
			if (ins->ta_priority < task->ta_priority)
				break;

		if (prev)
			STAILQ_INSERT_AFTER(&queue->tq_queue, prev, task, ta_link);
		else
			STAILQ_INSERT_HEAD(&queue->tq_queue, task, ta_link);
	}

	task->ta_pending = 1;
	if ((queue->tq_flags & TQ_FLAGS_BLOCKED) == 0) {
		if (queue->tq_enqueue)
			queue->tq_enqueue(queue->tq_context);
	} else {
		queue->tq_flags |= TQ_FLAGS_PENDING;
	}

	TQ_UNLOCK(queue);

	return 0;
}

void
taskqueue_block(struct taskqueue *queue)
{
	TQ_LOCK(queue);
	queue->tq_flags |= TQ_FLAGS_BLOCKED;
	TQ_UNLOCK(queue);
}

void
taskqueue_unblock(struct taskqueue *queue)
{
	TQ_LOCK(queue);
	queue->tq_flags &= ~TQ_FLAGS_BLOCKED;
	if (queue->tq_flags & TQ_FLAGS_PENDING) {
		queue->tq_flags &= ~TQ_FLAGS_PENDING;
		if (queue->tq_enqueue)
			queue->tq_enqueue(queue->tq_context);
	}
	TQ_UNLOCK(queue);
}

void
taskqueue_run(struct taskqueue *queue, int lock_held)
{
	struct task *task;
	int pending;

	if (lock_held == 0)
		TQ_LOCK(queue);
	while (STAILQ_FIRST(&queue->tq_queue)) {
		/*
		 * Carefully remove the first task from the queue and
		 * zero its pending count.
		 */
		task = STAILQ_FIRST(&queue->tq_queue);
		STAILQ_REMOVE_HEAD(&queue->tq_queue, ta_link);
		pending = task->ta_pending;
		task->ta_pending = 0;
		queue->tq_running = task;
		TQ_UNLOCK(queue);

		task->ta_func(task->ta_context, pending);

		TQ_LOCK(queue);
		queue->tq_running = NULL;
		wakeup(task);
	}
	if (lock_held == 0)
		TQ_UNLOCK(queue);
}

void
taskqueue_drain(struct taskqueue *queue, struct task *task)
{
	TQ_LOCK(queue);
	while (task->ta_pending != 0 || task == queue->tq_running)
		TQ_SLEEP(queue, task, "-");
	TQ_UNLOCK(queue);
}

static void
taskqueue_swi_enqueue(void *context)
{
	setsofttq();
}

static void
taskqueue_swi_run(void *arg, void *frame)
{
	taskqueue_run(taskqueue_swi, 0);
}

static void
taskqueue_swi_mp_run(void *arg, void *frame)
{
	taskqueue_run(taskqueue_swi_mp, 0);
}

int
taskqueue_start_threads(struct taskqueue **tqp, int count, int pri, int ncpu,
			const char *fmt, ...)
{
	__va_list ap;
	struct thread *td;
	struct taskqueue *tq;
	int i, error, cpu;
	char ktname[MAXCOMLEN];

	if (count <= 0)
		return EINVAL;

	tq = *tqp;
	cpu = ncpu;

	__va_start(ap, fmt);
	kvsnprintf(ktname, MAXCOMLEN, fmt, ap);
	__va_end(ap);

	tq->tq_threads = kmalloc(sizeof(struct thread *) * count, M_TASKQUEUE,
	    M_WAITOK | M_ZERO);

	for (i = 0; i < count; i++) {
		/*
		 * If no specific cpu was specified and more than one thread
		 * is to be created, we distribute the threads amongst all
		 * cpus.
		 */
		if ((ncpu <= -1) && (count > 1))
			cpu = i%ncpus;

		if (count == 1) {
			error = lwkt_create(taskqueue_thread_loop, tqp,
					    &tq->tq_threads[i], NULL,
					    TDF_NOSTART, cpu,
					    "%s", ktname);
		} else {
			error = lwkt_create(taskqueue_thread_loop, tqp,
					    &tq->tq_threads[i], NULL,
					    TDF_NOSTART, cpu,
					    "%s_%d", ktname, i);
		}
		if (error) {
			kprintf("%s: lwkt_create(%s): error %d", __func__,
			    ktname, error);
			tq->tq_threads[i] = NULL;
		} else {
			td = tq->tq_threads[i];
			lwkt_setpri_initial(td, pri);
			lwkt_schedule(td);
			tq->tq_tcount++;
		}
	}

	return 0;
}

void
taskqueue_thread_loop(void *arg)
{
	struct taskqueue **tqp, *tq;

	tqp = arg;
	tq = *tqp;
	TQ_LOCK(tq);
	while ((tq->tq_flags & TQ_FLAGS_ACTIVE) != 0) {
		taskqueue_run(tq, 1);
		TQ_SLEEP(tq, tq, "tqthr");
	}

	/* rendezvous with thread that asked us to terminate */
	tq->tq_tcount--;
	wakeup_one(tq->tq_threads);
	TQ_UNLOCK(tq);
	lwkt_exit();
}

void
taskqueue_thread_enqueue(void *context)
{
	struct taskqueue **tqp, *tq;

	tqp = context;
	tq = *tqp;

	wakeup_one(tq);
}

TASKQUEUE_DEFINE(swi, taskqueue_swi_enqueue, 0,
	 register_swi(SWI_TQ, taskqueue_swi_run, NULL, "swi_taskq", NULL, -1));
/*
 * XXX: possibly use a different SWI_TQ_MP or so.
 * related: sys/interrupt.h
 * related: platform/XXX/isa/ipl_funcs.c
 */
TASKQUEUE_DEFINE(swi_mp, taskqueue_swi_enqueue, 0,
    register_swi_mp(SWI_TQ, taskqueue_swi_mp_run, NULL, "swi_mp_taskq", NULL, 
		    -1));

struct taskqueue *taskqueue_thread[MAXCPU];

static void
taskqueue_init(void)
{
	int cpu;

	lockinit(&taskqueue_queues_lock, "tqqueues", 0, 0);
	STAILQ_INIT(&taskqueue_queues);

	for (cpu = 0; cpu < ncpus; cpu++) {
		taskqueue_thread[cpu] = taskqueue_create("thread", M_INTWAIT,
		    taskqueue_thread_enqueue, &taskqueue_thread[cpu]);
		taskqueue_start_threads(&taskqueue_thread[cpu], 1,
		    TDPRI_KERN_DAEMON, cpu, "taskq_cpu %d", cpu);
	}
}

SYSINIT(taskqueueinit, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, taskqueue_init, NULL);
