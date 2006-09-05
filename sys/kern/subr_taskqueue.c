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
 *	$DragonFly: src/sys/kern/subr_taskqueue.c,v 1.10 2006/09/05 00:55:45 dillon Exp $
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

#include <machine/ipl.h>

MALLOC_DEFINE(M_TASKQUEUE, "taskqueue", "Task Queues");

static STAILQ_HEAD(taskqueue_list, taskqueue) taskqueue_queues;

struct taskqueue {
	STAILQ_ENTRY(taskqueue)	tq_link;
	STAILQ_HEAD(, task)	tq_queue;
	const char		*tq_name;
	taskqueue_enqueue_fn	tq_enqueue;
	void			*tq_context;
	int			tq_draining;
};

struct taskqueue *
taskqueue_create(const char *name, int mflags,
		 taskqueue_enqueue_fn enqueue, void *context)
{
	struct taskqueue *queue;
	static int once = 1;

	queue = kmalloc(sizeof(struct taskqueue), M_TASKQUEUE, mflags);
	if (!queue)
		return 0;
	STAILQ_INIT(&queue->tq_queue);
	queue->tq_name = name;
	queue->tq_enqueue = enqueue;
	queue->tq_context = context;
	queue->tq_draining = 0;

	crit_enter();
	if (once) {
		STAILQ_INIT(&taskqueue_queues);
		once = 0;
	}
	STAILQ_INSERT_TAIL(&taskqueue_queues, queue, tq_link);
	crit_exit();

	return queue;
}

void
taskqueue_free(struct taskqueue *queue)
{
	crit_enter();
	queue->tq_draining = 1;
	crit_exit();

	taskqueue_run(queue);

	crit_enter();
	STAILQ_REMOVE(&taskqueue_queues, queue, taskqueue, tq_link);
	crit_exit();

	kfree(queue, M_TASKQUEUE);
}

struct taskqueue *
taskqueue_find(const char *name)
{
	struct taskqueue *queue;

	crit_enter();
	STAILQ_FOREACH(queue, &taskqueue_queues, tq_link) {
		if (!strcmp(queue->tq_name, name)) {
			crit_exit();
			return queue;
		}
	}
	crit_exit();
	return 0;
}

int
taskqueue_enqueue(struct taskqueue *queue, struct task *task)
{
	struct task *ins;
	struct task *prev;

	crit_enter();

	/*
	 * Don't allow new tasks on a queue which is being freed.
	 */
	if (queue->tq_draining) {
		crit_exit();
		return EPIPE;
	}

	/*
	 * Count multiple enqueues.
	 */
	if (task->ta_pending) {
		task->ta_pending++;
		crit_exit();
		return 0;
	}

	/*
	 * Optimise the case when all tasks have the same priority.
	 */
	prev = STAILQ_LAST(&queue->tq_queue, task, ta_link);
	if (!prev || prev->ta_priority >= task->ta_priority) {
		STAILQ_INSERT_TAIL(&queue->tq_queue, task, ta_link);
	} else {
		prev = 0;
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
	if (queue->tq_enqueue)
		queue->tq_enqueue(queue->tq_context);

	crit_exit();

	return 0;
}

void
taskqueue_run(struct taskqueue *queue)
{
	struct task *task;
	int pending;

	crit_enter();
	while (STAILQ_FIRST(&queue->tq_queue)) {
		/*
		 * Carefully remove the first task from the queue and
		 * zero its pending count.
		 */
		task = STAILQ_FIRST(&queue->tq_queue);
		STAILQ_REMOVE_HEAD(&queue->tq_queue, ta_link);
		pending = task->ta_pending;
		task->ta_pending = 0;
		crit_exit();

		task->ta_func(task->ta_context, pending);

		crit_enter();
	}
	crit_exit();
}

static void
taskqueue_swi_enqueue(void *context)
{
	setsofttq();
}

static void
taskqueue_swi_run(void *arg, void *frame)
{
	taskqueue_run(taskqueue_swi);
}

TASKQUEUE_DEFINE(swi, taskqueue_swi_enqueue, 0,
	 register_swi(SWI_TQ, taskqueue_swi_run, NULL, "swi_taskq", NULL));

static void
taskqueue_kthread(void *arg)
{
	for (;;) {
		taskqueue_run(taskqueue_thread[mycpuid]);
		crit_enter();
		if (STAILQ_EMPTY(&taskqueue_thread[mycpuid]->tq_queue))
			tsleep(taskqueue_thread[mycpuid], 0, "tqthr", 0);
		crit_exit();
	}
}

static void
taskqueue_thread_enqueue(void *context)
{
	wakeup(taskqueue_thread[mycpuid]);
}

struct taskqueue *taskqueue_thread[MAXCPU];
static struct thread *taskqueue_thread_td[MAXCPU];

static void
taskqueue_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus; cpu++) {
		taskqueue_thread[cpu] = taskqueue_create("thread", M_INTWAIT,
		    taskqueue_thread_enqueue, NULL);
		kthread_create(taskqueue_kthread, NULL,
		    &taskqueue_thread_td[cpu], "taskqueue");
	}
}

SYSINIT(taskqueueinit, SI_SUB_CONFIGURE, SI_ORDER_SECOND, taskqueue_init, NULL);
