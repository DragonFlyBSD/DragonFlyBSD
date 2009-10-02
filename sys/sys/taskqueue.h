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
 * $FreeBSD: src/sys/sys/taskqueue.h,v 1.3.2.2 2003/09/10 00:40:39 ken Exp $
 * $DragonFly: src/sys/sys/taskqueue.h,v 1.6 2005/09/21 18:58:55 hsu Exp $
 */

#ifndef _SYS_TASKQUEUE_H_
#define _SYS_TASKQUEUE_H_

#if !defined(_KERNEL)
#error "This file should not be included by userland programs."
#endif

#include <sys/queue.h>

struct taskqueue;

/*
 * Each task includes a function which is called from
 * taskqueue_run().  The first argument is taken from the 'ta_context'
 * field of struct task and the second argument is a count of how many
 * times the task was enqueued before the call to taskqueue_run().
 */
typedef void task_fn_t(void *context, int pending);

/*
 * A notification callback function which is called from
 * taskqueue_enqueue().  The context argument is given in the call to
 * taskqueue_create().  This function would normally be used to allow the
 * queue to arrange to run itself later (e.g., by scheduling a software
 * interrupt or waking a kernel thread).
 */
typedef void (*taskqueue_enqueue_fn)(void *context);

struct task {
	STAILQ_ENTRY(task) ta_link;	/* link for queue */
	int	ta_pending;		/* count times queued */
	int	ta_priority;		/* priority of task in queue */
	task_fn_t *ta_func;		/* task handler */
	void	*ta_context;		/* argument for handler */
};

struct taskqueue *taskqueue_create(const char *name, int mflags,
				    taskqueue_enqueue_fn enqueue,
				    void *context);
int	taskqueue_start_threads(struct taskqueue **tqp, int count, int pri,
				int ncpu, const char *name, ...)
				__printflike(5, 6);
int	taskqueue_enqueue(struct taskqueue *queue, struct task *task);
void	taskqueue_drain(struct taskqueue *queue, struct task *task);
struct taskqueue *taskqueue_find(const char *name);
void	taskqueue_free(struct taskqueue *queue);
void	taskqueue_block(struct taskqueue *queue);
void	taskqueue_unblock(struct taskqueue *queue);

/*
 * Functions for dedicated thread taskqueues
 */
void	taskqueue_thread_loop(void *arg);
void	taskqueue_thread_enqueue(void *context);

/*
 * Initialise a task structure.
 */
#define TASK_INIT(task, priority, func, context) do {	\
	(task)->ta_pending = 0;				\
	(task)->ta_priority = (priority);		\
	(task)->ta_func = (func);			\
	(task)->ta_context = (context);			\
} while (0)

/*
 * Declare a reference to a taskqueue.
 */
#define TASKQUEUE_DECLARE(name)			\
extern struct taskqueue *taskqueue_##name

/*
 * Define and initialise a taskqueue.
 */
#define TASKQUEUE_DEFINE(name, enqueue, context, init)			\
									\
struct taskqueue *taskqueue_##name;					\
									\
static void								\
taskqueue_define_##name(void *arg)					\
{									\
	taskqueue_##name =						\
	    taskqueue_create(#name, M_INTWAIT, (enqueue), (context));	\
	init;								\
}									\
									\
SYSINIT(taskqueue_##name, SI_SUB_CONFIGURE, SI_ORDER_SECOND,		\
	taskqueue_define_##name, NULL)					\
									\
struct __hack

#define	TASKQUEUE_DEFINE_THREAD(name)					\
TASKQUEUE_DEFINE(name, taskqueue_thread_enqueue, &taskqueue_##name,	\
	taskqueue_start_threads(&taskqueue_##name, 1, prio,		\
	"%s taskq", #name))
/*
 * This queue is serviced by a software interrupt handler.  To enqueue
 * a task, call taskqueue_enqueue(taskqueue_swi, &task).
 */
TASKQUEUE_DECLARE(swi);
TASKQUEUE_DECLARE(swi_mp);

/*
 * This queue is serviced by a per-cpu kernel thread.  To enqueue a task, call
 * taskqueue_enqueue(taskqueue_thread[mycpuid], &task).
 */
extern struct taskqueue *taskqueue_thread[];

#endif /* !_SYS_TASKQUEUE_H_ */
